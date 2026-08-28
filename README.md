# 🔄 Bazsi Router Reboot

ESP32-C3 alapú automatikus router újraindító rendszer. Az eszköz folyamatosan figyeli az internetkapcsolatot, és ha kiesést észlel, relén keresztül automatikusan újraindítja a routert.

## 📋 Jellemzők

- **Automatikus internetkapcsolat-figyelés** – öt HTTP teszt, öt különböző üzemeltető felé
- **Automatikus router újraindítás** – relé segítségével áramtalanítja, majd visszakapcsolja a routert
- **Wi-Fi Manager** – böngészőből konfigurálható SSID, jelszó, IP-cím és gateway
- **LittleFS** – a beállítások áramszünet után is megmaradnak
- **Deep Sleep védelem** – túl sok sikertelen próbálkozás után az ESP alvó módba lép (1 óra), majd újrapróbálkozik
- **Fizikai gombok** – reset és Wi-Fi reset gombok debounce-szal
- **Uptime kijelzés** – Soros porton folyamatosan látható az eszköz futási ideje

## 🔧 Hardver követelmények

| Komponens | Leírás |
|-----------|--------|
| **Mikrokontroller** | ESP32-C3 (pl. XIAO ESP32-C3) |
| **Relé modul** | 1 csatornás relé (router tápellátásának kapcsolására) |
| **LED #1** | Állapot LED (D4 pin) |
| **LED #2** | Wi-Fi állapot LED (D3 pin) |
| **Nyomógomb #1** | Reset gomb (D1 pin) – ESP újraindítás |
| **Nyomógomb #2** | Wi-Fi reset gomb (D0 pin) – mentett Wi-Fi adatok törlése |

### Pin kiosztás

| Pin | Funkció |
|-----|---------|
| `D0` | Wi-Fi reset gomb (INPUT_PULLUP) |
| `D1` | Reset / ébresztő gomb (INPUT_PULLUP) – RTC GPIO, deep sleepből is ébreszt |
| `D3` | Wi-Fi állapot LED |
| `D4` | Állapot LED |
| `D5` | Relé vezérlés (router tápellátás) |

## ⚙️ Működés

### 1. Első indulás – Wi-Fi konfiguráció

Ha nincs mentett Wi-Fi adat (vagy a Wi-Fi reset gombot megnyomtad):

1. Az ESP **Access Point módba** lép
2. Az AP neve: `ESP-<chipmodel>`, jelszó: `12345678`
3. Csatlakozz az AP-hez, majd nyisd meg a böngészőben: `192.168.4.1`
4. Töltsd ki az űrlapot:
   - **SSID** – a Wi-Fi hálózat neve (max. 32 karakter)
   - **Password** – a Wi-Fi jelszó (max. 63 karakter)
   - **IP Address** – az ESP kívánt statikus IP-je (opcionális, ha üres → DHCP)
   - **Gateway** – a router IP-je (opcionális, ha üres → DHCP)
5. Küldés után az ESP újraindul és csatlakozik a megadott hálózathoz

> **Statikus IP-hez mindkét mezőt ki kell tölteni.** Gateway nélkül a firmware
> DHCP-re esne vissza, ezért a beállító portál a félig kitöltött párost
> visszautasítja, ahelyett hogy sikert jelentene egy olyan fix címre, amit az
> eszköz végül nem használ. DHCP-hez hagyd mindkettőt üresen.
>
> **Egy elgépelt, de formailag helyes cím** (pl. `192.168.0.200` a
> `192.168.1.200` helyett) átmegy a validáción – ilyet semmilyen űrlap nem tud
> kiszűrni. Az eszköz futás közben veszi észre: ha a saját gateway-ét sem éri
> el, a router kap egy újraindítást, és ha utána sem érhető el, **AP beállító
> módba** megy, hogy javítani lehessen (a `/log` oldalon `GW UNREACH` és
> `AP MODE` 4 néven látszik).
>
> **Csak IPv4 cím adható meg** (és nem `0.0.0.0`). Az `IPAddress::fromString()`
> az IPv4 után IPv6-ot is megpróbál, ezért a `::1` vagy a `fe80::1` is
> érvényesnek *látszana* – a `WiFi.config()` viszont az `IPAddress` `uint32_t`
> konverzióját használja, ami IPv6-ra `0`-t ad. Egy IPv6 cím így csendben
> DHCP-t, IPv4-cím + IPv6-gateway párosnál pedig **gateway és elsődleges DNS
> nélküli** statikus konfigurációt eredményezne – vagyis nem lenne internet.
>
> **Az SSID és a jelszó elejéről/végéről a szóközök levágódnak**, már mentéskor.
> Így az elmentett érték pontosan az, amivel az eszköz csatlakozni fog, és a
> portál visszajelzése is ezt mutatja. (Következmény: olyan SSID vagy jelszó
> nem használható, aminek szándékosan szóköz van a szélén.)
>
> Az AP mód **5 perc tétlenség** után elalszik (minden HTTP kérés újraindítja a
> visszaszámlálást), és utána már csak a reset gomb vagy az áramtalanítás
> ébreszti fel. Fájlírás közben soha nem alszik el.
>
> **Amíg a lap nyitva van, nem alszik el:** a beállító oldal és a naplóoldal
> 60 mp-enként meghív egy `/ping` végpontot (1 bájt válasz), ami újraindítja a
> visszaszámlálást. Enélkül az óra a lapbetöltéstől ketyegne, nem az utolsó
> interakciótól – mérve: 6 percnyi lassú gépelés után a Submit már nem érte el
> az eszközt, és a portál visszahozásához fizikailag meg kellett nyomni a reset
> gombot. A lapot bezárva az utolsó pingtől számít újra az 5 perc.
> Statikus IP esetén a DNS szervernek a megadott gateway (tartalékként `1.1.1.1`)
> lesz beállítva, különben a névfeloldás – és így a HTTP-teszt – nem működne.
> (Az ESP32 `WiFi.config()` STA ágában a DNS-t szó szerint a kapott paraméterből
> veszi, gateway-fallback **nincs**, és a DHCP-t leállítja.)

### 2. Normál működés – Állapotgép

Az eszköz három állapotban működik:

```
┌─────────────┐
│  TESTING     │ ◄── Internetkapcsolat tesztelése
│  STATE       │
└──────┬───┬──┘
       │   │
  sikeres  sikertelen
       │   │
       ▼   ▼
┌──────────┐  ┌──────────────┐
│ SUCCESS  │  │  FAILURE     │
│ STATE    │  │  STATE       │
│ (1 perc  │  │ (12s várakoz │
│  várakoz)│  │  → újrateszt)│
└──────────┘  └──────────────┘
                     │
              3+ hiba ÉS cycleIndex > 3
                     │
                     ▼
              ┌──────────────┐
              │ ROUTER RESET │
              │ (relé ki/be) │
              │ 90s szünet   │
              │ + 10 perc    │
              │   várakozás  │
              └──────────────┘
```

#### Tesztelési módszerek (ciklikusan váltakoznak)

| Ciklus index | Végpont | Üzemeltető | Elvárt válasz |
|:---:|---|---|---|
| 0 | `msftconnecttest.com/connecttest.txt` | Microsoft | `Microsoft Connect Test` |
| 1 | `cp.cloudflare.com/generate_204` | Cloudflare | **204 No Content** |
| 2 | `detectportal.firefox.com/success.txt` | Mozilla | `success` |
| 3 | `nmcheck.gnome.org/check_network_status.txt` | GNOME / NetworkManager | `NetworkManager is online` |
| 4 | `connectivitycheck.gstatic.com/generate_204` | Google | **204 No Content** |
| 5+ | `msftconnecttest.com/connecttest.txt` | Microsoft | `Microsoft Connect Test` |

> Az 5+ sor a gyakorlatban nem fordul elő: mivel `failedCount == cycleIndex + 1`
> mindig igaz, a 4-es indexnél már teljesül a reset feltétele.

**Nincs internet = mind az öt teszt elbukik.** Öt különböző végpont, öt
független üzemeltető, két ellenőrzési mód. Bármelyik siker nullázza a
számlálókat, tehát egyetlen szolgáltató kiesése soha nem látszik
internetkimaradásnak. A küszöb szándékosan szigorúbb az iparági szokásnál
(a Nagios `max_check_attempts` alapértelmezése 3 egymást követő bukás), mert
egy téves reset ára ~11,5 perc kiesés, a plusz szigorúságé viszont csak
~1 perc késleltetés.

> **Ha DNS-szűrőt futtatsz a hálózaton** (Pi-hole, AdGuard Home), ellenőrizd a
> blokklistáidat: több népszerű lista tiltja a `connectivitycheck.gstatic.com`
> és a `detectportal.firefox.com` domaint (épp azért, hogy az OS/böngésző ne
> „telefonáljon haza"). Egy blokkolt név `0.0.0.0`-ra oldódik vagy NXDOMAIN-t
> ad, tehát az adott teszt **mindig elbukik**. Ez önmagában nem okoz téves
> resetet – a 2-es indexig csak akkor jutunk el, ha a 0-s és az 1-es is
> elbukott –, de elveszíted a redundancia egy részét. A soros naplóban a
> `Teszt ciklus index = N` sor és a `/log` oldal `TEST FAIL` bejegyzésének
> paramétere is megmondja, melyik végpont bukott el.

#### Miért nincs ping az internettesztek között

Az ICMP nem bizonyít sem névfeloldást, sem TCP-t. A leggyakoribb valós hiba
– amikor az olcsó router DNS-továbbítója befagy – mellett a ping tökéletesen
megy, miközben a házban egyetlen eszköz sem éri el az internetet. Mérve: a
korábbi, pinget is tartalmazó változat **egy órán át 41 bukott HTTP teszt
mellett nulla** router resetet indított, mert az 1-es indexen a `1.1.1.1`
ping mindig sikerült és nullázta a számlálókat. Nem véletlen, hogy egyetlen
nagy implementáció sem ICMP-vel validál: a NetworkManager (libcurl HTTP), a
Firefox (`detectportal`), a Windows NCSI (DNS + HTTP) mind HTTP-t használ.

A ping megmaradt, de **csak a saját gateway ellenőrzésére** (`gatewayUnreachable()`),
ahol pont az a kérdés, hogy a 3. rétegben elérünk-e egyáltalán valamit.

#### A két ellenőrzési mód

A **204-es ellenőrzés** szigorúbb, mint a szöveg-egyeztetés: egy captive
portál nem tud `204 No Content`-et adni, mert neki éppenséggel tartalmat kell
küldenie (bejelentkező oldal vagy átirányítás). A `testInternetHTTP()` akkor
vált erre az ágra, ha az elvárt válasz üres sztring. Ugyanezt a döntést hozza
a NetworkManager is (`src/core/nm-connectivity.c`: 204 → „no content, as
expected", bármi más → portál).

> A szöveges ellenőrzés a válaszból legfeljebb 96 bájtot olvas be, és levágja
> a záró sortörést az összehasonlítás előtt.

#### Router reset folyamat

1. Ha **3+ sikertelen teszt** és **cycleIndex > 3**:
   - Relé **bekapcsol** → router áramtalanítva
   - **90 másodperc** várakozás (RESET_PULSE)
   - Relé **kikapcsol** → router visszakap áramot
   - **10 perc** várakozás (RESET_DELAY) – idő a router bootolásához
   - Wi-Fi újracsatlakozás
2. Ha **4 router újraindítás** sem hozza vissza az internetet → deep sleep 1 órára,
   majd magától ébred és újrapróbálja. (A `maxfailureEvents = 5` a *reset esemény*
   számláló határa; a számláló még a reset előtt nő, ezért 4 tényleges újraindítás történik.)

### 3. Fizikai gombok

| Gomb | Funkció |
|------|---------|
| **Reset** (D1) | ESP32-C3 azonnali újraindítása; deep sleepből felébreszti az eszközt |
| **Wi-Fi Reset** (D0) | Mentett Wi-Fi adatok törlése + ESP újraindítás → visszaáll AP módba |

> ⚠️ Induláskor **mindkét** gombot ellenőrizzük. Ha bármelyik beragadva marad, a
> két LED 3 másodpercig **felváltva** villog (megkülönböztetésül a végzetes
> hibától, ahol együtt villognak), majd az ESP 60 másodpercre deep sleep módba lép.
> Ilyenkor a gombos ébresztés **nincs** bekapcsolva, különben a beragadt gomb végtelen boot loopot okozna.

### Deep sleep és ébredés

| | |
|---|---|
| **Elalvás oka** | 5 sikertelen router reset, vagy sikertelen Wi-Fi újracsatlakozás |
| **Alvás hossza** | 1 óra (timer), vagy 60 mp beragadt gomb esetén |
| **Ébresztés** | timer **vagy** a reset gomb (D1) lenyomása. Végzetes hiba utáni alvásnál **csak a gomb** |
| **Ébredés után** | teljes újraindulás – a `setup()` fut le elölről |

Az ESP32-C3 deep sleepből mindig **újraindulással** ébred: a RAM tartalma elvész,
így a hibaszámlálók (`resetEvents`, `failedCount`) nullázódnak, és az eszköz
tiszta lappal kezdi az internet tesztelését. A projekt nem használ RTC memóriát,
tehát alvás előtti állapot nem őrződik meg.

> ⚠️ **Hardver követelmény a relére.** Deep sleep alatt az ESP32-C3 digitális
> lábai (GPIO6–21) nagyimpedanciás állapotba kerülnek, és csak az RTC lábak
> (GPIO0–5) tarthatók meg hold funkcióval. A relé a `D5` = **GPIO7**-en van,
> tehát az alvás teljes ideje alatt **lebeg** – ezt szoftverből nem lehet
> megakadályozni. A relé vezérlőbemenetére **külső lehúzó ellenállás** kell
> (aktív-HIGH modulnál 10 kΩ a GND felé), különben az alvás alatt a lebegő láb
> véletlenül áramtalaníthatja a routert.
> (Forrás: ESP-IDF *Sleep Modes*, ESP32-C3 – „digital GPIOs (GPIO6 ~ 21) are in
> a high impedance state".)

## 📁 Projekt struktúra

```
bazsi-router-reboot/
├── bazsi_router_reboot.ino   # Fő Arduino sketch
├── data/
│   ├── wifimanager.html      # Wi-Fi beállító weboldal
│   ├── style.css             # Weboldal stílus
│   └── favicon.png           # Favicon
└── LICENSE                   # MIT License
```

## 📦 Szükséges könyvtárak

| Könyvtár | Leírás |
|----------|--------|
| `WiFi.h` | ESP32 Wi-Fi kezelés |
| `ESPAsyncWebServer` | Aszinkron webszerver |
| `AsyncTCP` | Aszinkron TCP az ESPAsyncWebServer-hez |
| `LittleFS` | Fájlrendszer a flash memóriában |
| `HTTPClient` | HTTP kérések az internettesztekhez |
| `ESPping` | ICMP ping a saját gateway ellenőrzéséhez |

## 🚀 Telepítés

### Arduino IDE

1. Telepítsd az **ESP32 board support**-ot az Arduino IDE-ben
2. Telepítsd a szükséges könyvtárakat (Library Manager vagy kézi telepítés)
3. Válaszd ki a board-ot: **ESP32-C3** (pl. *XIAO_ESP32C3*)
4. Nyisd meg a `bazsi_router_reboot.ino` fájlt
5. Töltsd fel a `data/` mappát a LittleFS-re:
   - **Arduino IDE 2.x**: használj [LittleFS upload plugin](https://github.com/earlephilhower/arduino-littlefs-upload)-t
   - Vagy: `Tools` → `ESP32 Sketch Data Upload`
6. Töltsd fel a sketch-et az ESP-re

### PlatformIO

```ini
[env:esp32c3]
platform = espressif32
board = seeed_xiao_esp32c3
framework = arduino
lib_deps =
    ESP Async WebServer
    AsyncTCP
    ESPping
board_build.filesystem = littlefs
```

## ⏱️ Időzítések

| Paraméter | Érték | Leírás |
|-----------|-------|--------|
| `interval` | 20s | Wi-Fi csatlakozási timeout |
| `SUCCESS_DELAY` | 1 perc | Várakozás sikeres teszt után |
| `PROBE_DELAY` | 12s | Várakozás sikertelen teszt után (újrapróbálkozás előtt) |
| `RESET_PULSE` | 90s | Router áramtalanítás időtartama |
| `RESET_DELAY` | 10 perc | Várakozás a router reset után (bootolási idő) |
| `firstStartDelay` | 10 perc | Első indítás utáni várakozás |
| `maxfailureEvents` | 5 | A reset esemény számláló határa → **4** tényleges router újraindítás után deep sleep |
| `wifi_maxRetries` | 3 | Wi-Fi újracsatlakozási próbálkozások |
| `wifiInterval` | 30s | Szünet a próbálkozások között |
| `MAX_RETRY_ROUNDS` | 33 | 33 × 85,5 perc = **47 óra** türelem, körönként egy router újraindítással |
| `AP_TIMEOUT_MS` | 5 perc | AP mód tétlenség után alvás |

## 📊 Soros monitor

A program `115200` baud rate-tel kommunikál a soros porton. Csatlakozz a soros monitorral az állapotok figyeléséhez:

```
Uptime: 0h 0m 1s
Init LittleFS.
LittleFS mounted successfully
Connecting to WiFi...
Connected to: MyNetwork
IP Address: 192.168.1.200
Signal strength (RSSI): -45 dBm
WIFI OK!
Uptime: 0h 3m 1s
Beginning Test.
Microsoft Connect Test
Igaz érték!
Successful Test
SUCCESS_DELAY delay start.
```

## ✅ Tesztelt konfiguráció

| | |
|---|---|
| **Board package** | ESP32 Arduino **3.3.11** |
| **Beágyazott ESP-IDF** | `release_v5.5` (`esp32-arduino-libs-idf-release_v5.5-b774170f`) |
| **Board** | XIAO ESP32-C3 (`D0`=GPIO2, `D1`=GPIO3, `D3`=GPIO5, `D4`=GPIO6, `D5`=GPIO7) |

A firmware viselkedését meghatározó feltételezések ezen a konkrét verzión lettek
ellenőrizve a core, illetve az ESP-IDF forrásában:

- `WiFi.disconnect(true)` → az első paraméter `wifioff`, **nem** `eraseap`
  (`WiFiSTA.h`), tehát a mentett hálózat nem törlődik.
- `WiFi.config(ip, gw, subnet, dns1, dns2)` STA ága a DNS-t **szó szerint** az
  átadott paraméterből veszi (`NetworkInterface.cpp` → `esp_netif_set_dns_info`),
  gateway-fallback nincs, a DHCP-t pedig leállítja. Ezért kell a DNS-t kézzel
  megadni.
- `disconnect(true)` → `STA.end()` → `enableSTA(false)` → `STAClass::onDisable()`,
  ami `_esp_netif = NULL; destroyNetif()`. A statikus IP/DNS és az
  `ESP_NETIF_HAS_STATIC_IP_BIT` elvész, a következő `connect()` pedig a bit
  hiányában `config()`-ot hív → DHCP. Ezért nem elég a nyers `WiFi.begin()`
  a router reset után.
- Deep sleepben az ESP32-C3 `GPIO6–21` lábai nagyimpedanciásak, csak a
  `GPIO0–5` tartható hold funkcióval (ESP-IDF *Sleep Modes*, esp32c3).
- `esp_deep_sleep_enable_gpio_wakeup()` ezen a néven létezik IDF 5.5-ben
  (IDF 6.0-ban átnevezték – a kód mindkét nevet kezeli), és csak RTC-képes
  lábat fogad el; a `D1` = GPIO3 megfelel.

## 🐕 Watchdog – védelem lefagyás ellen

Ha a program megakadna (végtelen ciklus, deadlock), a legrosszabb eset az, hogy
**a relé bekapcsolt állapotban ragad, és a router tartósan áram nélkül marad**.
A watchdog ezt oldja meg: újraindítja az ESP-t, a `setup()` pedig azonnal
`LOW`-ra állítja a relét, tehát a router visszakapja az áramot.

Az ESP-IDF task watchdogja alapból fut, de önmagában **nem véd meg**:

| | Érték a kész Arduino libekben | Következmény |
|---|---|---|
| `loopTaskWDTEnabled` | `false` (Arduino `main.cpp:111`) | a `loop()` megakadását észre sem veszi |
| `ESP_TASK_WDT_PANIC` | `y` (`defconfig.common:21`) – az IDF saját alapja `n` | jó, de nem a mi érdemünk: egy másképp fordított libnél a timeout csak figyelmeztetés lenne |
| `ESP_TASK_WDT_TIMEOUT_S` | `5` (IDF alap, a lib-builder nem írja felül) | rövidebb, mint a firmware szándékos blokkolásai |

> A „kész Arduino libek" értékei az
> [espressif/esp32-arduino-lib-builder](https://github.com/espressif/esp32-arduino-lib-builder)
> `configs/defconfig.common` és `configs/defconfig.esp32c3` fájljaiból valók –
> ezekből fordulnak az `esp32-arduino-libs` előfordított könyvtárak. Ami ott
> nem szerepel, arra az ESP-IDF Kconfig alapértelmezése érvényes.

Ezért az `initWatchdog()` mindhármat kifejezetten beállítja:

- **90 másodperces** timeout – a leghosszabb *nem etethető* blokkolás a
  `http.GET()`. A rossz eset nem a hallgató szerver (5 mp connect + 10 mp
  válasz = 15 mp), hanem a **halott DNS**, mert a névfeloldás a connect
  timeouton **kívül** esik – lásd lentebb
- **`trigger_panic = true`** – timeoutkor valódi újraindulás
- **`idle_core_mask = 0`** – csak a saját loop taskot figyeli. Az idle task
  figyelése itt káros lenne, mert a firmware szándékosan blokkol percekig
- a hosszú várakozások (`waitWithButtons`, `blockingDelay`, a 90 mp-es relé
  pulzus, az újracsatlakozás) **etetik** a watchdogot ~10 ms-onként

> A TWDT timeoutja globális, ezért az AsyncTCP saját taskjának figyelése is
> 5 mp-ről 90 mp-re lazul (`CONFIG_ASYNC_TCP_USE_WDT=1`, `AsyncTCP.cpp:334`).
> Ez a gyakorlatban nem számít: az aszinkron szerver csak AP konfigurációs
> módban fut, és ott a `loop()` amúgy is ezredmásodpercek alatt körbeér.

### Mikor élesedik

A watchdog a **LittleFS csatolása után, de a Wi-Fi indítása előtt** élesedik:

| Szakasz | Felügyelve? | Miért |
|---|:---:|---|
| soros port, gombellenőrzés, LittleFS csatolás/**formázás** | nem | a `LittleFS.begin(true)` első indításkor formáz: ~1,5 MB partíció szektoronként 30–50 ms, összesen 15–20 mp. Ezt szándékosan kihagyjuk, hogy egy első bekapcsolás soha ne fusson watchdog resetbe |
| `WiFi.persistent()`, `initWiFi()`, AP portál indítása | **igen** | a Wi-Fi init a legvalószínűbb lefagyási pont |
| `loop()` | **igen** | |

### Mit fog el a hardver magától

Az ESP32-C3 watchdogjai mind hardveresek (`SOC_WDT_SUPPORTED`):

| Watchdog | Állapot az Arduino buildben | Mit fog el |
|---|---|---|
| **INT_WDT** (interrupt) | **bekapcsolva, 300 ms** – `CONFIG_ESP_INT_WDT_TIMEOUT_MS=300` (`defconfig.common:17`); magát a `CONFIG_ESP_INT_WDT`-t a lib-builder sehol nem tiltja le, tehát marad az IDF `default y` | ha a FreeRTOS tick megáll: letiltott megszakítás, végtelen ciklus megszakításban |
| **TWDT** (task) | bekapcsolva (`ESP_TASK_WDT_EN`/`_INIT` IDF alap `y`), 5 mp, panic **`y`** (`defconfig.common:21`); az idle task nincs figyelve (`# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is not set`, `defconfig.esp32c3:2`) | egy figyelt task nem etet – ezt konfiguráljuk 90 mp-re |
| **RWDT** (RTC) | bootloader, 9000 ms | **csak a bootot** – az IDF `app_main()` előtt kikapcsolja (`BOOTLOADER_WDT_DISABLE_IN_USER_CODE` alapból `n`) |

Az INT_WDT és a TWDT megléte **fordítási idejű** (sdkconfig) döntés: ezek a
beállítások az előfordított `esp32-arduino-libs` könyvtárakba vannak beleégetve.
Vázlatból nincs API, amivel az INT_WDT-t be lehetne kapcsolni – ha ki lenne
kapcsolva, csak saját IDF-fordítás vagy az `esp32-arduino-lib-builder`
segítene. A TWDT-ből ezzel szemben van futásidejű API (`esp_task_wdt_*`), és a
firmware ezt használja is.

#### Miért 90 mp, és nem 30

A `NetworkClient::connect(host, port, timeout)` **először** névfeloldást végez,
és az `http.setConnectTimeout()` erre **nem vonatkozik**:

```cpp
int NetworkClient::connect(const char *host, uint16_t port, int32_t timeout_ms) {
  IPAddress srv((uint32_t)0);
  if (!Network.hostByName(host, srv)) {   // <- nincs timeout paraméter
    return 0;
  }
  return connect(srv, port, timeout_ms);
}
```

Egy lwIP DNS lekérdezés **szerverenként ~7 mp** alatt adja fel: `DNS_MAX_RETRIES`
= 4 (lwIP `opt.h`), `DNS_TMR_INTERVAL` = 1000 ms (`dns.h`), és a `dns_check_entry()`
a `tmr`-t 1, 1, 2, 3 lépésekben növeli – összesen 7 tick. DHCP-től jellemzően
2 szerver jön (a 3. slot a fallback, `LWIP_FALLBACK_DNS_SERVER_SUPPORT` alapból `n`).

Ha az eszköznek van **globális IPv6 címe** (a lib-builder `CONFIG_LWIP_IPV6_AUTOCONFIG=y`),
a `hostByName()` **kétszer** kérdez: előbb csak `AF_INET6`-ot, aztán `AF_UNSPEC`-et.

Ráadásul a `lwip_getaddrinfo()` hibakódjai **pozitívak** (`netdb.h`: `EAI_NONAME`
200 … `HOST_NOT_FOUND` 210), amit a `!Network.hostByName(...)` igaznak lát – így
a sikertelen névfeloldás után még egy `0.0.0.0`-ra irányuló `connect()` is lefut
a maga 5 másodpercével.

| eset | egy bukott HTTP teszt |
|---|---|
| szerver hallgat, DNS jó | 15 mp |
| 1 DNS szerver, nincs IPv6 | ~12 mp |
| 2 DNS szerver, nincs IPv6 | ~19 mp |
| 2 DNS szerver + globális IPv6 | **~33 mp** |

A 90 mp tehát ~2,7-szeres tartalékot ad, nem hatszorosat. A `WD13` teszt ezt a
33 mp-es legrosszabb esetet méri.

A „kemény" megállást tehát az INT_WDT 300 ms alatt elkapja, akkor is, ha a
task watchdog még nem élesedett. Amit **csak** a task watchdog lát, az a
csendes, szabályosan blokkoló beragadás – amikor a rendszer tovább tickel, de
a mi taskunk örökre vár valamire. Épp ezért kellett a Wi-Fi init elé.

### A feliratkozás ellenőrzése

Az `enableLoopWDT()` a core-ban **`void`** (`esp32-hal-misc.c`): ha az
`esp_task_wdt_add()` hibázik, csak egy belső `log_e()` jelzi, a visszatérési
értékből semmi. Ez akkor fordulhat elő, ha a TWDT nincs inicializálva
(`ESP_TASK_WDT_INIT=n`) – ilyenkor az `add()` `ESP_ERR_INVALID_STATE`-et ad.

Ezért az `initWatchdog()`:

1. ha kell, maga hívja az `esp_task_wdt_init()`-et (figyelt task nélkül ez még
   nem indítja el a timert),
2. **utána** iratkozik fel – ez a sorrend azért kell, mert az
   `esp_task_wdt_reconfigure()` csak nem üres tasklistánál indítja újra a
   timert (`task_wdt.c:595`),
3. végül `esp_task_wdt_status(NULL)`-lal **visszakérdezi**, hogy a `loop()`
   tényleg figyelve van-e.

Csak ha ez igaz, írja ki, hogy `Watchdog enabled`. Ha nem, egyértelmű
figyelmeztetést ad, és **nem etet**: feliratkozás nélkül minden
`feedLoopWDT()` `ESP_ERR_NOT_FOUND`-ot kapna, amire a core `log_e()`-t hív –
mérve **100 hibasor percenként** normál működésben, védelem nélkül.

### Ismétlődő watchdog újraindulás

Ha a program ismételten megakad, az újraindítgatás önmagában nem megoldás.
**3 watchdog/panic miatti újraindulás után** az eszköz ugyanúgy leáll, mint a
többi végzetes hibánál: mindkét LED villog, 5 perc után deep sleep, és **csak a
reset gomb ébreszti**.

- A számláló `RTC_NOINIT_ATTR`-ben van. Ez **nem** ugyanaz, mint a deep sleepnél
  használt `RTC_DATA_ATTR`: az utóbbit egy watchdog reset újrainicializálná,
  azaz épp azt veszítenénk el, amit számolni akarunk.
- Rendellenesnek számít: `ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT`, `ESP_RST_WDT`,
  `ESP_RST_PANIC`, `ESP_RST_CPU_LOCKUP`.
- **Áramtalanítás** vagy külső reset nullázza (emberi beavatkozás → tiszta lap).
- **1 óra hibátlan működés** szintén nullázza.

Az **interrupt watchdog** (`ESP_INT_WDT`) az Arduino buildben aktív
(lásd fentebb: `defconfig.common:17`), és a panic kezelő alapértelmezése
`PRINT_REBOOT`, tehát a megszakítás-szintű megakadásokat az IDF már eleve
kezeli.

> A hosszú várakozások `delay()`-t használnak `yield()` helyett. A `yield()`
> csak azonos prioritású taskok között ad át vezérlést, tehát a korábbi
> változat a 90 mp-es relé pulzus alatt 100%-on pörgette a CPU-t; a `delay()`
> `vTaskDelay()`-re fordul, ami ténylegesen felfüggeszti a taskot.

## 📶 Ha nem sikerül csatlakozni a Wi-Fihez

Egységes politika **minden** ágon: **3 próba, köztük 30 másodperc szünet**
(próbánként 20 mp csatlakozási timeout).

### Induláskor

| Helyzet | Viselkedés |
|---|---|
| Nincs mentett SSID | Azonnal **AP beállító portál** |
| Van SSID, de nem érhető el | **10 perc várakozás** (`firstStartDelay`), majd 3 próba → ha így sem megy: **AP portál** |

A 10 perces várakozás a lényeg: áramszünet után az ESP másodpercek alatt
elindul, a router viszont percekig bootol.

### Működés közben megszakad a kapcsolat

```
kapcsolatvesztés → 3 próba (30 mp szünetekkel)
                 → sikertelen → ROUTER ÚJRAINDÍTÁS (relé ki 90 mp, be)
                 → 10 perc várakozás (RESET_DELAY)
                 → 3 próba (30 mp szünetekkel)
                 → sikertelen → AP beállító portál
```

### Az AP beállító mód

Az AP mód **5 perc** tétlenség után deep sleepbe megy, **időzített ébresztés
nélkül** – ugyanúgy, mint a végzetes LittleFS hibánál. Visszahozni a reset
gombbal vagy áramtalanítással lehet.

Két védelem gondoskodik arról, hogy a mentés ne vesszen el:

- **Minden HTTP kérés újraindítja az 5 perces visszaszámlálást** – a 404-es
  válasszal végződő is. A határidő abszolút: mindig pontosan 5 perccel a
  *legutolsó* kérés utánra kerül, nem halmozódik, és felső korlát nincs.
- **Fájlírás közben az eszköz soha nem alszik el** (`savingConfig` jelző), így
  nem maradhat félig kiírt konfiguráció.

## 🚨 Végzetes hiba – a fájlrendszer nem használható

A wifi beállítások a LittleFS-en vannak. Ha ez **nem használható**, a program
nem fut tovább: nem tesztel, nem kapcsolja a relét, és AP módba sem lép.
Mindkét LED (`D3` és `D4`) **együtt, gyorsan villog** (5 Hz).

Fontos a megkülönböztetés – a „nincs még konfiguráció" **nem hiba**:

| Helyzet | Minősítés | Viselkedés |
|---|---|---|
| A konfig fájlok nem léteznek (első indítás) | `CONFIG_MISSING` | normális → **AP beállító portál** |
| A fájlok léteznek, de üresek (wifireset után) | `CONFIG_OK`, üres érték | normális → **AP beállító portál** |
| A LittleFS nem csatolható | hiba | **hibajelzés**, villogás |
| A fájl létezik, de nem nyitható / hibás olvasás | `CONFIG_ERROR` | **hibajelzés**, villogás |
| A wifireset gomb **nem tudta törölni** a mentett adatokat | hiba | **hibajelzés**, villogás |

Az utolsó eset ugyanaz a hibaosztály: ha a fájlrendszer nem írható, akkor új
konfigurációt sem lehetne menteni. Az újraindítás csak a régi adatokkal hozná
vissza az eszközt – a gomb kívülről nézve „nem csinálna semmit", és a
felhasználó soros kábel nélkül sosem tudná meg, miért. Ezért itt sem indulunk
újra, hanem jelzünk.

Hibajelzés közben:

- a **relé `LOW`** marad, tehát a router végig kap áramot
- a **reset gomb mindig él**: bármikor újraindítja az eszközt. A wifireset gomb
  az induláskor felismert hibáknál szintén él (a `loop()` figyeli), a *sikertelen
  törlés* miatti hibajelzésnél viszont nem – oda épp a wifireset gombtól
  érkeztünk, önmagát hívná újra
- a watchdog aktív
- **5 perc után az eszköz deep sleepbe megy** – és **időzített ébresztés nélkül**.
  Egy sérült fájlrendszer magától nem gyógyul meg, ezért értelmetlen lenne
  óránként felébredni és újra villogni. Visszahozni a **reset gombbal** vagy
  áramtalanítással lehet.

## 💾 LittleFS hibakezelés

A beállítások LittleFS-en tárolódnak, ezért minden fájlművelet hibája
kezelve van – és ami fontosabb, **egyik sem hazudik sikert**.

| Hiba | Viselkedés |
|---|---|
| A fájlrendszer nem csatolható | Konkrét ok kiírása (a `begin()` csak `ESP_FAIL`-nél formáz, hiányzó partíciónál nem), majd végzetes hibajelzés – lásd fent |
| Hiányzó konfigurációs fájl | `CONFIG_MISSING` – nem hiba, AP beállító portál |
| A fájl nem nyitható írásra | `writeConfigValue()` `false`-t ad, a mentés nem történik meg |
| Rövid írás (megtelt FS) | A `print()` visszatérési értéke ellenőrizve, `false` |
| Az írás „sikerült", de a tartalom hibás | **Visszaolvasásos ellenőrzés** fogja meg |
| A törlés (csonkolás) nem megy | Tartalék: a fájl törlése `remove()`-val |
| Mentés a beállító oldalról | Hiba esetén HTTP 500 magyarázattal és **nincs újraindítás** |
| Érvénytelen űrlapadat (rossz SSID hossz, IP formátum, hiányzó SSID) | Ugyanúgy HTTP 500 az ok megnevezésével, **nincs újraindítás** |

A visszaolvasás azért kell, mert a `File::close()` és a `File::flush()` is
`void` a core-ban – a lezáráskor jelentkező hibát másképp nem lehetne észlelni.

A beállító oldalról mentés hibája korábban a legkellemetlenebb módon jelent
volna meg: az eszköz „Done"-t válaszol, újraindul, és mivel nem mentett semmit,
ismét AP módban jön fel – a felhasználó pedig végtelen körben próbálkozik
magyarázat nélkül. Most a hibát megkapja, és az eszköz nem indul újra, tehát a
beírt adatok sem vesznek el.

## 🔒 Biztonság

- A LittleFS-en tárolt Wi-Fi adatok (`/ssid.txt`, `/pass.txt`, `/ip.txt`,
  `/gateway.txt`) **nem érhetők el a webszerveren keresztül** – a beállító
  portál csak a `/`, `/style.css` és `/favicon.png` útvonalakat szolgálja ki.
- A jelszó soha nem kerül ki nyílt szövegként a soros portra, csak a hossza.
- Az AP jelszava (`12345678`) a forráskódban van; éles használat előtt
  érdemes lecserélni az `AP_PASSWORD` konstansban. A hosszát `static_assert`
  őrzi (WPA2: 8–63 karakter), mert a core rövidebbnél nem indítana AP-t, és a
  `softAP()` visszatérési értékét nem nézzük.

### A mentett jelszó összekeverése

A `/pass.txt` nem nyílt szöveggel tárolódik:

```
v1:ef58b21d923ed7afdc03f7e8336431cc8f158ea5
```

**Amit ad:**

| | |
|---|---|
| `strings dump.bin` a flash dumpon | nem ad használható jelszót (még 4 karakteres darabját sem) |
| Kimásolt `/pass.txt` másik lapkán | nem működik |
| Kész dekóder a nyilvános forrásból | önmagában nem elég – kell hozzá a konkrét chip |

A kulcsfolyam magjában ott van az **eFuse MAC** is
(`esp_efuse_mac_get_default()`, `Esp.cpp`). Az eFuse **nem a flashben van**,
ezért egy önmagában kimásolt flash-tartalom kevés hozzá.

**Amit NEM ad – ez fontos:**

> Ez **nem titkosítás**. Aki kódot tud futtatni az eszközön – az ESP32-C3-ban
> beépített USB Serial/JTAG-gel (`SOC_USB_SERIAL_JTAG_SUPPORTED`), vagy egy
> saját sketch-csel –, az a visszafejtett jelszót kiolvassa a RAM-ból: a
> művelet ugyanis magán az eszközön történik, az eszköz maga a visszafejtő.
> Ez a `strings | grep` utat zárja le, nem a felkészült támadót.
>
> **Az egyetlen valódi védelem a flash titkosítás** (XTS-AES-128, eFuse-ban
> tárolt kulccsal – a C3 tudja: `SOC_FLASH_ENCRYPTION_XTS_AES_128`). Ára:
> egyirányú eFuse-égetés és körülményesebb újraflashelés.
>
> A legjobb ár/érték arányú lépés viszont nem szoftveres: **tedd az eszközt
> vendég- vagy IoT-SSID-re**. Akkor egy ellopott eszközből csak a
> mellékhálózat kulcsa nyerhető ki.

**Kompatibilitás és hibatűrés:**

- A formátum verziózott (`v1:`). Az előtag nélküli fájl **régi, nyílt szöveges
  mentés** – azt továbbra is elfogadja, tehát egy frissítés nem teszi
  használhatatlanná a már beállított eszközöket.
- Hibás tartalom **nem** végzetes hiba: az eszköz sima szövegként kezeli, a
  Wi-Fi nem jön össze, és a szokásos úton AP módba kerül, ahol újra
  beállítható. Ez öngyógyul; a villogó LED nem.
- **Lapkacsere esetén újra kell konfigurálni** – a mentés az adott chiphez
  kötött. Ugyanezért egy LittleFS-mentés sem hordozható át másik eszközre.

## 📄 Licenc

MIT License – lásd a [LICENSE](LICENSE) fájlt.

## 📊 Működési táblázat

Az eszköz teljes viselkedése – mikor mit csinál és meddig, minden esettel:
[MUKODES.md](MUKODES.md)

## 🔍 Diagnosztikai napló

Az utolsó 32 esemény RTC memóriában, a beállító portál **`/log`** oldalán
olvasható – soros kábel nélkül is. Túléli a deep sleepet, a watchdog resetet és
a reset gombot; csak az áramtalanítás törli. Részletek:
[MUKODES.md](MUKODES.md)

## 🧪 Tesztek

A firmware vezérlési logikája hardver nélkül is tesztelhető: a `test/` mappa a
tényleges sketch-et fordítja host gcc-vel, stub ESP32 API-k felett.

```bash
cd test && make test
```

Részletek: [test/README.md](test/README.md)
