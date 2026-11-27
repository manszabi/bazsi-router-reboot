# Folyamatábrák

A firmware **jelenlegi** működésének pontos folyamatábrái, [Mermaid](https://mermaid.js.org/)
jelöléssel – a GitHub és a GitLab natívan rendereli, a forrás pedig a kóddal
együtt verziókezelhető és diff-elhető. Minden időzítés és feltétel a
`bazsi_router_reboot.ino` konstansaiból származik; a doboz-feliratok a tényleges
függvény- és konstansneveket használják, hogy az ábra a kódból ellenőrizhető
legyen.

Jelölés: lekerekített doboz = belépési/kilépési pont, rombusz = döntés,
téglalap = művelet. Az `EV_*` címkék a diagnosztikai napló eseménykódjai
(lásd `MUKODES.md` 15. fejezet). A `saveEventLog()` a naplót a LittleFS-re is
kiírja – **öt** fontos pillanatban: **router reset előtt**, **AP módba váltás
előtt**, **minden `enterDeepSleep()`-en át vezető alvás előtt** (egyetlen közös
ponton, a függvény elején), a **heap miatti** és a **Wi-Fi miatti** saját
újraindulás előtt;
így egy áramszünet sem viszi el. A mentés **hozzáfűz**, nem ír felül: a fájl
saját, 128 bejegyzéses gyűrű, az RTC napló 32-es gyűrűje pedig csak a legutóbbi
mentés óta eltelt időt őrzi. Egyetlen kivétel a beragadt gomb 60 mp-es alvása:
az a `setup()` elején, a `handleStuckButton()`-ból megy el, saját úton – ott a
LittleFS még csatolva sincs, tehát nem is volna mit írni.

---

## 1. Üzemmódok – madártávlat

Az eszköz minden ébredése teljes újraindulás (a deep sleep is), ezért a teljes
életciklus a `setup()`-ból indul. **Három** üzemmód van (`deviceMode`:
`MODE_MONITOR`, `MODE_CONFIG`, `MODE_FATAL`); alvásból mindig a `setup()`-ba
tér vissza.

```mermaid
stateDiagram-v2
    [*] --> Setup: bekapcsolás / reset / ébredés
    Setup --> MODE_MONITOR: WiFi OK, vagy first start várakozás
    Setup --> MODE_CONFIG: nincs mentett SSID
    Setup --> MODE_FATAL: LittleFS / konfig / 3x watchdog hiba
    Setup --> StuckSleep: beragadt gomb
    MODE_MONITOR --> MODE_CONFIG: rossz jelszó / 48 órája nincs Wi-Fi / rossz statikus IP
    MODE_MONITOR --> Restart: Wi-Fi-lépcső 1. foka (ESP-újraindítás)
    MODE_MONITOR --> Restart: tartósan kritikus heap (önkéntes újraindulás)
    MODE_MONITOR --> MODE_FATAL: 3x heap-újraindulás sem segített
    MODE_CONFIG --> ApSleep: 5 perc tétlenség (csak gombbal ébred)
    MODE_CONFIG --> Restart: sikeres mentés (2 s múlva)
    MODE_FATAL --> FatalSleep: 5 perc villogás után (csak gombbal ébred)
    StuckSleep --> Setup: 60 s timer
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
    WDT --> STUCK{"Nyomva van valamelyik gomb?<br>reset (D1) vagy wifireset (D0) = LOW"}
    STUCK -->|igen| HOLD{"Elengedik 3 s-on belül?<br>(STUCK_BUTTON_CONFIRM_MS)<br>a gombos ébresztés alatt a gomb<br>MINDIG lenyomva találtatik"}
    HOLD -->|"igen – ez ébresztő gombnyomás volt"| BOOTLOG
    HOLD -->|"nem – tényleg beragadt"| SBLOG["EV_BOOT + EV_STUCK_BUTTON naplózása<br>(csak az első körben – az ismétlődő<br>60 s-os ébredések némák)"]
    SBLOG --> SBLINK["3 s: a két LED FELVÁLTVA villog"]
    SBLINK --> SB60["Deep sleep 60 s<br>csak timer ébresztés, gomb NEM<br>(boot loop ellen)"]
    SB60 --> PWR
    STUCK -->|nem| BOOTLOG["EV_BOOT naplózása"]
    BOOTLOG --> ARM0["armButtonLatches()<br>a gomb-megszakítások élesítése<br>(a beragadt-gomb ellenőrzés UTÁN)"]
    ARM0 --> WCHK{"checkWatchdogResets()<br>rendellenes reset volt?<br>(TASK_WDT / INT_WDT / PANIC / LOCKUP)"}
    WCHK -->|"igen, sorozatban a 3."| FATAL["enterFatal()<br>MODE_FATAL, EV_FATAL(3)"]
    WCHK -->|"nem, vagy még 3 alatt"| HC["initHeapState() + applyHeapCarry()<br>a heap-újraindulás átvitele: resetEvents (pontosan egyszer)<br>initWifiLadder(): a Wi-Fi-lépcső (magic-kel);<br>ha az 1. fokon áll, a first start kimarad"]
    FATAL --> HC
    HC --> FS{"initLittleFS()<br>begin(formatOnFail = true)<br>formázás: 128 szektor, 4-7 s"}
    FS -->|"csatolás sikertelen"| FATAL2["enterFatal()<br>EV_FATAL(1)"]
    FS -->|ok| CFG{"Konfig olvasása:<br>/ssid.txt /pass.txt /ip.txt /gateway.txt<br>(jelszó: v1 hexa dekódolás)"}
    CFG -->|"fájl létezik, de olvashatatlan"| FATAL3["enterFatal()<br>EV_FATAL(2)"]
    CFG -->|"ok vagy hiányzó fájl"| PERS["WiFi.persistent(false)"]
    FATAL2 --> PERS
    FATAL3 --> PERS
    PERS --> ISFATAL{MODE_FATAL?}
    ISFATAL -->|igen| TOLOOP([loop: hibajelzés])
    ISFATAL -->|nem| WIFI{"initWiFi()<br>statikus IP ha érvényes, különben DHCP<br>csatlakozás max 20 s"}
    WIFI -->|siker| MON["MODE_MONITOR<br>EV_WIFI_OK (param: a lépcső foka)<br>WiFi LED = HIGH"]
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
    IN([MODE_MONITOR, firstStart = true<br>MINDKÉT úton ide jutunk: a WiFi MÁR élhet<br>ha az initWiFi sikerült, vagy még nem jött össze]) --> PRB{"onlineProbe() – az ELSŐ próba azonnal<br>esedékes, utána 60 s-onként<br>1. Wi-Fi: van kapcsolat? (ha nincs: csak<br>aszinkron begin(), következmény nélkül)<br>2. CSAK ha van: internetOk()<br>(ping 1.1.1.1 → 8.8.8.8 → HTTP)"}
    PRB -->|"mindkettő OK"| EARLY["firstStart AZONNAL lezárva<br>(nem várjuk ki a maradékot)"]
    EARLY --> DONE
    PRB -->|"bármelyik bukik"| WAIT{"Eltelt már 10 perc?<br>(firstStartDelay)"}
    WAIT -->|"nem – 60 s múlva ÚJRA próbál"| BACK([vissza a loop-ba<br>gombok és watchdog élnek])
    BACK -.->|"a következő ütem"| PRB
    WAIT -->|igen| REC{"reconnectWifi()<br>3 próba, 30 s szünetekkel<br>(próbánként 20 s timeout)"}
    REC -->|siker| DONE["firstStart lezárva<br>normál monitor üzem (4. ábra)"]
    REC -->|sikertelen| GU["wifiFailed(hidegindulás)<br>(6. ábra) – az ESP-újraindítás kimarad,<br>az első lépés a router reset"]
```

---

## 4. `loop()` diszpécser és a monitor állapotgép

```mermaid
flowchart TD
    L([loop ciklus, 10 ms-onként]) --> SER["serialCommands()<br>LOG = a teljes napló (fájl + RTC)<br>HELP = a parancsok listája<br>nem blokkol, MINDEN üzemmódban fut"]
    SER --> RP{"restartPending és<br>letelt a 2 s türelmi idő?"}
    RP -->|igen| WFW["lockConfigBeforeShutdown()<br>(fájlírás megvárása max 5 s,<br>ÉS a zár megszerzése)"] --> RS([ESP.restart])
    RP -->|nem| WDC0["1 óra hibátlan futás után:<br>watchdog + heap számláló nullázása"]
    WDC0 --> HEAP["checkHeap() – 10 s-onként mér<br>30 percenként állapotsor<br>25 kB alatt figyelmeztet (csak az átlépéskor)<br>KRITIKUS: szabad &lt; 12 kB VAGY legnagyobb tömb &lt; 6 kB<br>ha 3 egymást követő mérésen át kitart (30 s):<br>ÖNKÉNTES ÚJRAINDULÁS – csak MODE_MONITOR-ban,<br>fájlírás / relé-impulzus / reset-ellenőrző ablak alatt SOHA<br>EV_HEAP_RESTART + saveEventLog() a flashbe,<br>a resetEvents átvitelével, a Wi-Fi-lépcső mentésével"]
    HEAP --> NTP["ensureNtp()<br>óraszinkron indítása, amint van kapcsolat<br>(bármelyik úton jött is létre)"]
    NTP --> MODE{deviceMode}
    MODE -->|MODE_FATAL| FB["Mindkét LED együtt villog (5 Hz)<br>gombok élnek"] --> F5{"5 perc letelt?"}
    F5 -->|igen| FSL["fatalSleep() – EV_SLEEP(4)<br>alvás timer NÉLKÜL"]
    F5 -->|nem| L
    MODE -->|MODE_CONFIG| APB["Wi-Fi LED villog 1 Hz<br>státusz LED végig be"] --> APT{"5 perc tétlenség,<br>nincs mentés, nincs restart?"}
    APT -->|igen| ASL["apSleep() – EV_SLEEP(3)<br>alvás timer NÉLKÜL"]
    APT -->|nem| L
    MODE -->|MODE_MONITOR| FST{firstStart?}
    FST -->|igen| HFS["handleFirstStart() (3. ábra)"] --> L
    FST -->|nem| SM{currentState}

    SM -->|CHECK_STATE| WL{"WiFi.status()<br>== WL_CONNECTED?"}
    WL -->|nem| RC{"EV_WIFI_LOST (csak a sorozat 1.)<br>a lépcső várakozása alatt 1 próba (initWiFi),<br>különben 3 próba (reconnectWifi)"}
    RC -->|siker| L
    RC -->|sikertelen| GUP["wifiFailed() (6. ábra):<br>rossz jelszó → AP · ESP-újraindítás ·<br>router reset · várakozás a lépcső szerint"] --> L
    WL -->|igen| NET{"internetOk()<br>ping 1.1.1.1 → ping 8.8.8.8 →<br>HTTP a soron következő végpontra"}
    NET -->|"bármelyik siker"| SUC["kiesés vége, resetEvents = 0"] --> DNSC["routerDnsCheck()<br>(4b. ábra)"]
    DNSC -->|"nincs reset"| WAIT0["WAIT_STATE"] --> L
    DNSC -->|"reset indult"| L
    NET -->|"mind hallgat"| FST1["a kiesés első köre:<br>EV_TEST_FAIL + EV_HTTP_ERROR"]
    FST1 --> GWP{"gatewayPingOk()<br>válaszol a router?"}
    GWP --> DUE{"letelt a várakozás?<br>1. reset: 2 perc (router sem) / 15 perc (router él)<br>utána: 30 → 60 → 120 → 240 perc az előző óta"}
    DUE -->|nem| WAIT2["kiírja a hátralévő percet<br>WAIT_STATE"] --> L
    DUE -->|igen| RST

    SM -->|WAIT_STATE| SD{"1 perc letelt<br>a kör kezdete óta?"}
    SD -->|igen| BT["CHECK_STATE"] --> L
    SD -->|nem| L

    SM -->|RESET_STATE| RW["várakozás a router bootjára<br>(5. ábra)"] --> L
```

### Tartalék HTTP végpontok (sorban váltakoznak)

| # | Végpont | Elvárt válasz |
|:-:|---|---|
| 1 | `http://www.msftconnecttest.com/connecttest.txt` | `Microsoft Connect Test` |
| 2 | `http://cp.cloudflare.com/generate_204` | 204 No Content |
| 3 | `http://detectportal.firefox.com/success.txt` | `success` |
| 4 | `http://nmcheck.gnome.org/check_network_status.txt` | `NetworkManager is online` |
| 5 | `http://connectivitycheck.gstatic.com/generate_204` | 204 No Content |

### 4b. A router DNS-ének ellenőrzése (`routerDnsCheck()`)

```mermaid
flowchart TD
    IN([a ping szerint van internet]) --> DUE{"esedékes?<br>15 percenként, gyanúnál percenként"}
    DUE -->|nem| NO([nincs reset])
    DUE -->|igen| ISR{"a DNS szerver<br>a router (gateway)?"}
    ISR -->|nem| SKIP["kimarad (egyszer kiírja)"] --> NO
    ISR -->|igen| Q1{"testDnsServer(router)<br>2 próba × 3 s, közvetlen UDP"}
    Q1 -->|"válasz, hibakód 0 vagy 3"| OK["gyanú vége,<br>DNS-lépcső nullázva"] --> NO
    Q1 -->|"hallgat / hibás válasz"| Q2{"testDnsServer(1.1.1.1)<br>kontroll"}
    Q2 -->|hallgat| NOTR["nem a router hibája"] --> NO
    Q2 -->|válaszol| SUS["gyanú (kezdete rögzítve)"]
    SUS --> WIN{"letelt?<br>1. reset: 15 perc folyamatos gyanú<br>utána: 30 → 60 → 120 → 240 perc"}
    WIN -->|nem| NO
    WIN -->|igen| RST["dnsState.resetEvents++<br>startRouterReset(NET DIAG 3)"]
```

---

## 5. Router reset (`startRouterReset()` és `RESET_STATE`)

```mermaid
flowchart TD
    IN([Letelt a várakozás, elveszett a Wi-Fi, vagy a router DNS-e hallgat]) --> D["EV_NET_DIAG<br>1 = a router sem válaszol, 2 = a router él,<br>3 = a router DNS-e hallgat"]
    D --> GW1{"NET DIAG 1, statikus IP<br>és Wi-Fi kapcsolat?"}
    GW1 -->|igen| GWL["EV_GW_UNREACHABLE(1)"] --> CNT
    GW1 -->|nem| CNT["resetEvents++, lastResetAt = most<br>EV_ROUTER_RESET<br>saveEventLog() – MÉG a relé előtt"]
    CNT --> P1["Relé = HIGH: router áram nélkül<br>90 s (RESET_PULSE)<br>státusz LED VILLOG 2 Hz, Wi-Fi LED sötét"]
    P1 --> P2["Relé = LOW: router bootol<br>RESET_STATE, max 10 perc (RESET_DELAY)<br>gombok + watchdog élnek"]
    P2 --> PRB{"percenként: onlineProbe()<br>1. Wi-Fi: van kapcsolat? (ha nincs: csak<br>aszinkron begin(), következmény nélkül)<br>2. CSAK ha van: internetOk()"}
    PRB -->|"mindkettő OK"| GW2
    PRB -->|"még nem, és letelt a 10 perc"| P3["WiFi.disconnect(true)<br>reconnectWifi(): 3 próba, 30 s szünet<br>(a statikus IP/DNS újra beáll)"]
    PRB -->|"még nem, és van hátra idő"| P2
    P3 -->|sikertelen| GU["wifiFailed() (6. ábra)"]
    P3 -->|siker| GW2{"Statikus IP, és a gateway<br>továbbra sem pingelhető?"}
    GW2 -->|igen| APX["EV_GW_UNREACHABLE(2) + EV_AP_MODE(4)<br>startConfigPortal()<br>(valószínűleg rossz a statikus IP)"]
    GW2 -->|nem| OK["CHECK_STATE"]
```

---

## 6. `wifiFailed()` – a Wi-Fi-lépcső

```mermaid
flowchart TD
    IN([a Wi-Fi-próbák elbuktak]) --> AUTH{"WL_CONNECT_FAILED?<br>(rossz jelszó)"}
    AUTH -->|igen| AP2["lépcső törlése<br>startConfigPortal(), EV_AP_MODE(2)"]
    AUTH -->|nem| GIVE{"48 órája nincs Wi-Fi?<br>(WIFI_GIVEUP_MIN)"}
    GIVE -->|igen| AP3["lépcső törlése<br>startConfigPortal(), EV_AP_MODE(3)"]
    GIVE -->|nem| S0{"0. fok, és nem hidegindulás?"}
    S0 -->|igen| RST["1. fok mentése az RTC-be (magic-kel)<br>EV_WIFI_RESTART, saveEventLog()<br>ESP.restart() – a router érintetlen"]
    S0 -->|nem| PEND{"2+. fok, és a lépcső szerint<br>még nem esedékes?<br>30 → 60 → 120 → 240 perc"}
    PEND -->|igen| WAIT["WAIT_STATE: percenként 1 próba"]
    PEND -->|nem| RR["fok++ (legalább 2)<br>startRouterReset(NET DIAG 1)"]
```

A lépcső **30 perc stabil Wi-Fi után** nullázódik (`wifiConnectedTick()`). Az
áramszünet, a gombok, a mentés utáni újraindulás és minden alvás törli; a saját,
a heap miatti és a watchdog-újraindulást túléli (`RTC_NOINIT`).

---

## 7. AP beállító portál (MODE_CONFIG)

```mermaid
flowchart TD
    IN([startConfigPortal]) --> AP["WIFI_AP mód<br>SSID: ESP-ESP32-C3-XXXX (MAC vége)<br>jelszó: eszközönként egyedi (/appass.txt)<br>végpontok: GET / , /ping, /log és POST /"]
    AP --> IDLE["5 perces tétlenségi visszaszámlálás<br>MINDEN kérés (404 is) újraindítja;<br>a nyitott lap 60 s-onként /ping-el"]
    IDLE --> REQ{Kérés típusa}
    REQ -->|"GET /"| FORM["Beépített beállító űrlap (sendConfigForm)<br>SSID/IP/gateway ELŐKITÖLTVE, escape-elve;<br>a jelszó SOHA. A LittleFS-ről semmit<br>nem szolgálunk ki"]
    REQ -->|"GET /log"| LOG["Diagnosztikai napló: reset ok, számlálók,<br>a FÁJL teljes tartalma (max 128), majd az RTC-ből<br>a mentés óta keletkezett rész (max 32)<br>Forrás oszlop: fajl / RTC, kettőzés nincs"]
    REQ -->|"POST /"| FSOK{"fsReady?<br>(csatolva van a LittleFS?)"}
    FSOK -->|nem| E500F["500: LittleFS nem elerheto<br>(partíciós séma hiba)"]
    FSOK -->|igen| VAL{"1. FÁZIS - validálás:<br>mind a négy mezőn ELŐBB trim, AZTÁN hossz<br>SSID 1-32, jelszó max 63,<br>IP és gateway: IPv4, nem 0.0.0.0<br>statikus IP CSAK párban"}
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
    L->>L: lockConfigBeforeShutdown() – max 5 s, a zárat meg is szerzi
    L->>L: ESP.restart()
```

---

## 8. Elalvás és ébredés

Minden alvás a közös `enterDeepSleep()`-en megy át:

```mermaid
flowchart TD
    IN([enterDeepSleep timerUs]) --> SV["saveEventLog()<br>a napló kiírása a fájlrendszerre<br>(minden alvás előtt)"]
    SV --> W["lockConfigBeforeShutdown()<br>fájlírás közben nem alszunk<br>(a zárat meg is szerzi)"]
    W --> OFF["LED-ek + relé = LOW<br>relé hold: gpio_hold_en + deep_sleep_hold<br>WiFi le, webszerver le, Serial le"]
    OFF --> DET["A gomb-megszakítások leválasztása<br>(ugyanazok a GPIO regiszterek)"]
    DET --> ARM["Minden ébresztőforrás törlése,<br>majd: reset gomb (D1 = GPIO3, RTC-képes)<br>élesítése LOW szintre"]
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
| `apSleep()` | AP portál 5 perc tétlenség | `EV_SLEEP(3)` | – | igen |
| `fatalSleep()` | végzetes hiba, 5 perc villogás után | `EV_SLEEP(4)` | – | igen |
| beragadt gomb\* | reset/wifireset LOW induláskor **és 3 s után is** | `EV_STUCK_BUTTON` | 60 s | **nem** (szándékosan) |

\* Ez az egyetlen alvás, ami **nem** az `enterDeepSleep()`-en megy át: a
`handleStuckButton()` a `setup()` elején, saját úton alszik el (nincs
`saveEventLog()` – a LittleFS ott még nincs csatolva; és szándékosan nem
armol gombébresztést, különben a beragadt gomb azonnal boot loopot okozna).

Ébredéskor az eszköz teljesen újraindul (`setup()`, 2. ábra); a RAM-beli
számlálók (`testState`, `timing`, `uiFlags`) nullázódnak. Ami az RTC
memóriában túléli:

| Változó | Tárolás | Mit él túl? |
|---|---|---|
| diagnosztikai napló (`rtcEvMagic`, `rtcEvNext`, `rtcEvents`) | `RTC_NOINIT` | deep sleep, watchdog reset, reset gomb – csak az áramtalanítás törli |
| `rtcSavedEvNext` (a legutóbbi fájlmentés pozíciója) | `RTC_NOINIT` | ugyanaz |
| watchdog számláló (`rtcWdtMagic`, `rtcWdtResets`) | `RTC_NOINIT` | ugyanaz |
| heap-újraindulások és az átvitel (`rtcHeapMagic`, `rtcHeapRestarts`, `rtcCarryResetEvents`) | `RTC_NOINIT` | ugyanaz |
| a Wi-Fi-lépcső (`rtcWifiMagic`, `rtcWifiStep`, `rtcWifiOutageMin`) | `RTC_NOINIT` | a saját, a heap miatti és a watchdog-újraindulást – az áramszünet, a gombok és az alvás törlik |

A **valós idő** (`gettimeofday()`) szintén túléli a deep sleepet – az RTC
órából jön –, míg a `millis()` minden ébredéskor nulláról indul.

---

*Az ábrák a `bazsi_router_reboot.ino` aktuális állapotát dokumentálják.
Módosításkor a kóddal együtt frissítendők – a viselkedést a `test/` alatti
370 forgatókönyves (2195 ellenőrzéses) tesztkészlet rögzíti.*
