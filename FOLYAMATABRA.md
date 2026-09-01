# Folyamatábrák

A firmware **jelenlegi** működésének pontos folyamatábrái, [Mermaid](https://mermaid.js.org/)
jelöléssel – a GitHub és a GitLab natívan rendereli, a forrás pedig a kóddal
együtt verziókezelhető és diff-elhető. Minden időzítés és feltétel a
`bazsi_router_reboot.ino` konstansaiból származik; a doboz-feliratok a tényleges
függvény- és konstansneveket használják, hogy az ábra a kódból ellenőrizhető
legyen.

Jelölés: lekerekített doboz = belépési/kilépési pont, rombusz = döntés,
téglalap = művelet. Az `EV_*` címkék a diagnosztikai napló eseménykódjai
(lásd `MUKODES.md` 12. fejezet).

---

## 1. Üzemmódok – madártávlat

Az eszköz minden ébredése teljes újraindulás (a deep sleep is), ezért a teljes
életciklus a `setup()`-ból indul. Négy üzemmód van; alvásból mindig a
`setup()`-ba tér vissza.

```mermaid
stateDiagram-v2
    [*] --> Setup: bekapcsolás / reset / ébredés
    Setup --> MODE_MONITOR: WiFi OK, vagy first start várakozás
    Setup --> MODE_CONFIG: nincs mentett SSID
    Setup --> MODE_FATAL: LittleFS / konfig / 3x watchdog hiba
    Setup --> StuckSleep: beragadt gomb
    MODE_MONITOR --> MODE_CONFIG: rossz jelszó / 33 kör letelt / rossz statikus IP
    MODE_MONITOR --> RetrySleep: hálózat nincs (1 óra, max 33 kör)
    MODE_MONITOR --> NetFailSleep: 4 router reset után sincs internet (1 óra)
    MODE_CONFIG --> ApSleep: 5 perc tétlenség (csak gombbal ébred)
    MODE_CONFIG --> Restart: sikeres mentés (2 s múlva)
    MODE_FATAL --> FatalSleep: 5 perc villogás után (csak gombbal ébred)
    StuckSleep --> Setup: 60 s timer
    RetrySleep --> Setup: 1 óra timer vagy reset gomb
    NetFailSleep --> Setup: 1 óra timer vagy reset gomb
    ApSleep --> Setup: reset gomb
    FatalSleep --> Setup: reset gomb
    Restart --> Setup
```

---

## 2. `setup()` – indulási folyamat

```mermaid
flowchart TD
    PWR([Bekapcsolás / reset / ébredés]) --> PINS["GPIO-k beállítása<br>relé = LOW, státusz LED = HIGH<br>relé hold feloldása (gpio_hold_dis)"]
    PINS --> SER["Serial indítása<br>max 3 s várakozás + 0,5 s CDC-beállás"]
    SER --> WDT["initWatchdog()<br>TWDT: 90 s, trigger_panic = true<br>a loop task feliratkoztatva<br>innentől a setup() is felügyelt"]
    WDT --> STUCK{"Beragadt gomb?<br>reset (D1) vagy wifireset (D0) = LOW"}
    STUCK -->|igen| SBLOG["EV_BOOT + EV_STUCK_BUTTON naplózása<br>(csak az első körben – az ismétlődő<br>60 s-os ébredések némák)"]
    SBLOG --> SBLINK["3 s: a két LED FELVÁLTVA villog"]
    SBLINK --> SB60["Deep sleep 60 s<br>csak timer ébresztés, gomb NEM<br>(boot loop ellen)"]
    SB60 --> PWR
    STUCK -->|nem| BOOTLOG["EV_BOOT naplózása"]
    BOOTLOG --> WCHK{"checkWatchdogResets()<br>rendellenes reset volt?<br>(TASK_WDT / INT_WDT / PANIC / LOCKUP)"}
    WCHK -->|"igen, sorozatban a 3."| FATAL["enterFatal()<br>MODE_FATAL, EV_FATAL(3)"]
    WCHK -->|"nem, vagy még 3 alatt"| FS{"initLittleFS()<br>begin(formatOnFail = true)<br>formázás: 128 szektor, 4-7 s"}
    FATAL --> FS
    FS -->|"csatolás sikertelen"| FATAL2["enterFatal()<br>EV_FATAL(1)"]
    FS -->|ok| CFG{"Konfig olvasása:<br>/ssid.txt /pass.txt /ip.txt /gateway.txt<br>(jelszó: v1 hexa dekódolás)"}
    CFG -->|"fájl létezik, de olvashatatlan"| FATAL3["enterFatal()<br>EV_FATAL(2)"]
    CFG -->|"ok vagy hiányzó fájl"| PERS["WiFi.persistent(false)"]
    FATAL2 --> PERS
    FATAL3 --> PERS
    PERS --> ISFATAL{MODE_FATAL?}
    ISFATAL -->|igen| TOLOOP([loop: hibajelzés])
    ISFATAL -->|nem| WIFI{"initWiFi()<br>statikus IP ha érvényes, különben DHCP<br>csatlakozás max 20 s"}
    WIFI -->|siker| MON["MODE_MONITOR<br>EV_WIFI_OK, rtcRetryRounds = 0<br>WiFi LED = HIGH"]
    WIFI -->|"ssid üres"| AP["startConfigPortal()<br>MODE_CONFIG, EV_AP_MODE(1)"]
    WIFI -->|"ssid van, de nem érhető el"| FIRST["MODE_MONITOR<br>first start várakozás indul<br>(3. ábra)"]
    MON --> TOLOOP2([loop: ELŐBB handleFirstStart 3. ábra,<br>utána az állapotgép])
    FIRST --> TOLOOP2
    AP --> TOLOOP3([loop: portál])
```

---

## 3. First start – türelmi idő induláskor

Áramszünet után a router lassabban bootol, mint az ESP; ezért mentett SSID
mellett az eszköz nem megy azonnal AP módba. A 10 perc **felső korlát**:
60 másodpercenként megnézzük, hogy visszajött-e a hálózat és az internet, és
ha mindkettő megvan, a várakozás azonnal véget ér.

```mermaid
flowchart TD
    IN([MODE_MONITOR, firstStart = true<br>MINDKÉT úton ide jutunk: a WiFi MÁR élhet<br>ha az initWiFi sikerült, vagy még nem jött össze]) --> PRB{"60 s-onként: onlineProbe()<br>1. Wi-Fi: van kapcsolat? (ha nincs: csak<br>aszinkron begin(), következmény nélkül)<br>2. CSAK ha van: ping 1.1.1.1 (DNS nélkül)"}
    PRB -->|"mindkettő OK"| EARLY["firstStart AZONNAL lezárva<br>(nem várjuk ki a maradékot)"]
    EARLY --> DONE
    PRB -->|"bármelyik bukik"| WAIT{"Eltelt már 10 perc?<br>(firstStartDelay)"}
    WAIT -->|"nem – 60 s múlva ÚJRA próbál"| BACK([vissza a loop-ba<br>gombok és watchdog élnek])
    BACK -.->|"a következő ütem"| PRB
    WAIT -->|igen| REC{"reconnectWifi()<br>3 próba, 30 s szünetekkel<br>(próbánként 20 s timeout)"}
    REC -->|siker| DONE["firstStart lezárva<br>normál monitor üzem (4. ábra)"]
    REC -->|sikertelen| AUTH{"WL_CONNECT_FAILED?<br>(hitelesítési hiba)"}
    AUTH -->|igen| GU["wifiGiveUp() (6. ábra)"]
    AUTH -->|nem| RST{"routerResetAndRetry():<br>relé 90 s ki + max 10 perc bootvárás<br>(60 s-onként onlineProbe, korai kilépéssel)<br>+ reconnectWifi()"}
    RST -->|siker| DONE
    RST -->|sikertelen| GU
```

---

## 4. `loop()` diszpécser és a monitor állapotgép

```mermaid
flowchart TD
    L([loop ciklus, 10 ms-onként]) --> RP{"restartPending és<br>letelt a 2 s türelmi idő?"}
    RP -->|igen| WFW["waitForConfigWrite()<br>(fájlírás megvárása, max 5 s)"] --> RS([ESP.restart])
    RP -->|nem| MODE{deviceMode}
    MODE -->|MODE_FATAL| FB["Mindkét LED együtt villog (5 Hz)<br>gombok élnek"] --> F5{"5 perc letelt?"}
    F5 -->|igen| FSL["fatalSleep() – EV_SLEEP(4)<br>alvás timer NÉLKÜL"]
    F5 -->|nem| L
    MODE -->|MODE_CONFIG| APB["Wi-Fi LED villog 1 Hz<br>státusz LED végig be"] --> APT{"5 perc tétlenség,<br>nincs mentés, nincs restart?"}
    APT -->|igen| ASL["apSleep() – EV_SLEEP(3)<br>alvás timer NÉLKÜL"]
    APT -->|nem| L
    MODE -->|MODE_MONITOR| FST{firstStart?}
    FST -->|igen| HFS["handleFirstStart() (3. ábra)"] --> L
    FST -->|nem| WDC["1 óra hibátlan futás után:<br>watchdog számláló nullázása"]
    WDC --> SM{currentState}

    SM -->|TESTING_STATE| WL{"WiFi.status()<br>== WL_CONNECTED?"}
    WL -->|nem| RC{"EV_WIFI_LOST<br>reconnectWifi()"}
    RC -->|siker| L
    RC -->|sikertelen| FRC["cycleIndex = 4<br>FAILURE_STATE<br>(azonnali reset ág)"] --> L
    WL -->|igen| TEST["HTTP teszt a cycleIndex szerinti<br>végponton (lásd táblázat)<br>rtcRetryRounds = 0"]
    TEST -->|siker| SUC["SUCCESS_STATE<br>cycleIndex, failedCount,<br>resetEvents = 0"] --> L
    TEST -->|sikertelen| FAI["failedCount++<br>EV_TEST_FAIL (csak az 1.)<br>FAILURE_STATE"] --> L

    SM -->|SUCCESS_STATE| SD{"60 s letelt?<br>(SUCCESS_DELAY)"}
    SD -->|igen| BT["TESTING_STATE"] --> L
    SD -->|nem| L

    SM -->|FAILURE_STATE| CI{"cycleIndex > 3?<br>(mind az 5 végpont elbukott)"}
    CI -->|nem| PD{"12 s letelt?<br>(PROBE_DELAY)"}
    PD -->|igen| NEXT["cycleIndex++<br>TESTING_STATE"] --> L
    PD -->|nem| L
    CI -->|igen| RESET["Router reset szekvencia<br>(5. ábra)"] --> L
```

### Teszt végpontok (ciklikus eszkaláció)

| Kiírt sorszám | `cycleIndex` | Végpont | Elvárt válasz |
|:---:|:---:|---|---|
| 1 | 0 | `http://www.msftconnecttest.com/connecttest.txt` | `Microsoft Connect Test` |
| 2 | 1 | `http://cp.cloudflare.com/generate_204` | 204 No Content |
| 3 | 2 | `http://detectportal.firefox.com/success.txt` | `success` |
| 4 | 3 | `http://nmcheck.gnome.org/check_network_status.txt` | `NetworkManager is online` |
| 5 | 4 | `http://connectivitycheck.gstatic.com/generate_204` | 204 No Content |

A soros port és a `/log` oldal **1-től** számol, a `cycleIndex` változó viszont
0-alapú marad – ahhoz kötődik a végpontválasztó `if`-lánc és a
`RESET_TRIGGER_CYCLE` küszöb is.

Router reset csak akkor indul, ha **mind az öt** üzemeltető végpontja elbukott
– egyetlen szolgáltató kiesése így sosem látszik internetkimaradásnak.

---

## 5. Router reset szekvencia (FAILURE, `cycleIndex > 3`)

```mermaid
flowchart TD
    IN([Mind az 5 végpont elbukott]) --> GW1{"Statikus IP aktív ÉS<br>a saját gateway sem pingelhető?"}
    GW1 -->|igen| GWL["EV_GW_UNREACHABLE(1)<br>a router kap még egy esélyt"]
    GW1 -->|nem| CNT
    GWL --> CNT["resetEvents++<br>EV_ROUTER_RESET"]
    CNT --> MX{"resetEvents >= 5?"}
    MX -->|"igen (már 4 reset volt)"| NFS["internetFailSleep()<br>EV_SLEEP(2)<br>deep sleep 1 óra"]
    MX -->|nem| P1["Relé = HIGH: router áram nélkül<br>90 s (RESET_PULSE)<br>státusz LED VILLOG 2 Hz, Wi-Fi LED sötét"]
    P1 --> P2["Relé = LOW: router bootol<br>max 10 perc várakozás (RESET_DELAY)<br>gombok + watchdog élnek"]
    P2 --> PRB{"60 s-onként: onlineProbe()<br>1. Wi-Fi: van kapcsolat? (ha nincs: csak<br>aszinkron begin(), következmény nélkül)<br>2. CSAK ha van: ping 1.1.1.1"}
    PRB -->|"mindkettő OK"| GW2
    PRB -->|"még nem, és letelt a 10 perc"| P3["WiFi.disconnect(true)<br>reconnectWifi(): 3 próba, 30 s szünet<br>(a statikus IP/DNS újra beáll)"]
    PRB -->|"még nem, és van hátra idő"| P2
    P3 -->|sikertelen| GU["wifiGiveUp() (6. ábra)"]
    P3 -->|siker| GW2{"A gateway továbbra<br>sem pingelhető?"}
    GW2 -->|igen| APX["EV_GW_UNREACHABLE(2) + EV_AP_MODE(4)<br>startConfigPortal()<br>(valószínűleg rossz a statikus IP)"]
    GW2 -->|nem| OK["TESTING_STATE<br>cycleIndex = 0, failedCount = 0"]
```

---

## 6. `wifiGiveUp()` – a 46 órás kitartási politika

```mermaid
flowchart TD
    IN([3 csatlakozási próba elbukott]) --> AUTH{"WL_CONNECT_FAILED?<br>(explicit hitelesítési hiba)"}
    AUTH -->|"igen – rossz jelszó"| AP2["startConfigPortal()<br>EV_AP_MODE(2)"]
    AUTH -->|nem| INC["rtcRetryRounds++<br>(RTC memória: túléli a deep sleepet,<br>bekapcsolásra nullázódik)"]
    INC --> R33{"rtcRetryRounds >= 33?<br>(MAX_RETRY_ROUNDS)"}
    R33 -->|"igen – ~46 óra telt el"| AP3["startConfigPortal()<br>EV_AP_MODE(3)"]
    R33 -->|nem| SL["retrySleep()<br>EV_SLEEP(1)<br>deep sleep 1 óra, majd új kör"]
```

Egy kör ébren töltött ideje ~25,5 perc (10 perc várakozás + 3 próba + 90 s
reset + 10 perc bootvárás + 3 próba). A két 10 perces tétel **felső korlát** –
de erre a számításra ez nem hat: ha a hálózat végig halott (márpedig ez a
szakasz pontosan arról szól), az `onlineProbe()` sosem sikerül, és a körök
teljes hosszukban futnak. A körök között 1 óra alvás:
33 × 25,5 perc + 32 × 60 perc ≈ **46 óra** türelem, mielőtt AP módba vált
(mérve: `R8` teszt).

---

## 7. AP beállító portál (MODE_CONFIG)

```mermaid
flowchart TD
    IN([startConfigPortal]) --> AP["WIFI_AP mód<br>SSID: ESP-ESP32-C3, jelszó: 12345678<br>végpontok: GET / , /ping, /log és POST /"]
    AP --> IDLE["5 perces tétlenségi visszaszámlálás<br>MINDEN kérés (404 is) újraindítja;<br>a nyitott lap 60 s-onként /ping-el"]
    IDLE --> REQ{Kérés típusa}
    REQ -->|"GET /"| FORM["Beépített beállító űrlap (CONFIG_FORM)<br>a flashből; a LittleFS-ről semmit<br>nem szolgálunk ki"]
    REQ -->|"GET /log"| LOG["Diagnosztikai napló:<br>reset ok, számlálók, 32 esemény"]
    REQ -->|"POST /"| VAL{"1. FÁZIS - validálás:<br>SSID 1-32 (trim), jelszó max 63 (trim),<br>IP és gateway: IPv4, nem 0.0.0.0, trim,<br>statikus IP CSAK párban"}
    VAL -->|hiba| E500["500 + konkrét indok<br>SEMMI nem íródik ki, a futó konfig<br>sem változik, NINCS újraindítás"]
    VAL -->|"minden mező érvényes"| LOCK{"Zár megszerzése<br>(beginConfigWrite, atomikus)"}
    LOCK -->|"foglalt (wifireset töröl)"| E503["503: próbáld újra<br>egy pillanat múlva"]
    LOCK -->|ok| SAVE["2. FÁZIS - commit:<br>globálisok + fájlok írása<br>visszaolvasó ellenőrzéssel<br>(jelszó: v1 + XOR-hexa kódolás)<br>savingConfig = false"]
    SAVE -->|"írási hiba"| E500W["500: LittleFS írási hiba<br>NINCS újraindítás"]
    SAVE -->|ok| R200["200: Done. ESP will restart...<br>restartPending = true"]
    R200 --> DEF["A loop 2 s türelmi idő után<br>(esetleges 2. mentést megvárva)<br>ESP.restart()"]
```

### A mentés két taskja (halasztott újraindítás)

```mermaid
sequenceDiagram
    participant B as Böngésző
    participant W as async_tcp task (webszerver)
    participant L as loop task
    B->>W: POST / (ssid, pass, ip, gateway)
    W->>W: savingConfig = true
    W->>W: validálás + 4 fájl írása és visszaolvasása
    W->>W: savingConfig = false
    W-->>B: 200 "Done. ESP will restart..."
    W->>L: restartPending = true, restartAt = most + 2 s
    Note over L: savingConfig alatt tilos: gombkezelés,<br>alvás, újraindítás (sérült konfig ellen)
    L->>L: 2 s türelmi idő (dupla mentés beérhet)
    L->>L: waitForConfigWrite() – max 5 s
    L->>L: ESP.restart()
```

---

## 8. Elalvás és ébredés

Minden alvás a közös `enterDeepSleep()`-en megy át:

```mermaid
flowchart TD
    IN([enterDeepSleep timerUs]) --> W["waitForConfigWrite()<br>fájlírás közben nem alszunk"]
    W --> OFF["LED-ek + relé = LOW<br>relé hold: gpio_hold_en + deep_sleep_hold<br>WiFi le, webszerver le, Serial le"]
    OFF --> ARM["Minden ébresztőforrás törlése,<br>majd: reset gomb (D1 = GPIO3, RTC-képes)<br>élesítése LOW szintre"]
    ARM --> TMR{"timerUs > 0?"}
    TMR -->|igen| T1["Timer élesítése"]
    TMR -->|"nem, DE a gombélesítés hibázott"| T2["Biztonsági háló:<br>1 órás timer mégis"]
    TMR -->|"nem, gomb OK"| T3["Nincs timer –<br>csak a gomb ébreszt"]
    T1 --> DS([esp_deep_sleep_start])
    T2 --> DS
    T3 --> DS
```

| Alvás | Kiváltó ok | Napló | Timer | Reset gomb ébreszt? |
|---|---|---|:---:|:---:|
| `retrySleep()` | a hálózat nem látszik, kör < 33 | `EV_SLEEP(1)` | 1 óra | igen |
| `internetFailSleep()` | 4 router reset után sincs internet | `EV_SLEEP(2)` | 1 óra | igen |
| `apSleep()` | AP portál 5 perc tétlenség | `EV_SLEEP(3)` | – | igen |
| `fatalSleep()` | végzetes hiba, 5 perc villogás után | `EV_SLEEP(4)` | – | igen |
| beragadt gomb | reset/wifireset LOW induláskor | `EV_STUCK_BUTTON` | 60 s | **nem** (szándékosan) |

Ébredéskor az eszköz teljesen újraindul (`setup()`, 2. ábra); a RAM-beli
számlálók nullázódnak, az RTC memóriában csak a `rtcRetryRounds`, a watchdog
számláló és a diagnosztikai napló él túl.

---

*Az ábrák a `bazsi_router_reboot.ino` aktuális állapotát dokumentálják.
Módosításkor a kóddal együtt frissítendők – a viselkedést a `test/` alatti
208 forgatókönyves (678 ellenőrzéses) tesztkészlet rögzíti.*
