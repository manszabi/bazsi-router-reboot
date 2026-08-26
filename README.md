# 🔄 Bazsi Router Reboot

ESP32-C3 alapú automatikus router újraindító rendszer. Az eszköz folyamatosan figyeli az internetkapcsolatot, és ha kiesést észlel, relén keresztül automatikusan újraindítja a routert.

## 📋 Jellemzők

- **Automatikus internetkapcsolat-figyelés** – HTTP és ICMP (ping) tesztek váltakozásával, két különböző publikus szerver felé
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
2. Az AP neve: `ESP-<chipmodel>`, jelszó: `bazsi1234`
3. Csatlakozz az AP-hez, majd nyisd meg a böngészőben: `192.168.4.1`
4. Töltsd ki az űrlapot:
   - **SSID** – a Wi-Fi hálózat neve (max. 32 karakter)
   - **Password** – a Wi-Fi jelszó (max. 63 karakter)
   - **IP Address** – az ESP kívánt statikus IP-je (opcionális, ha üres → DHCP)
   - **Gateway** – a router IP-je (opcionális, ha üres → DHCP)
5. Küldés után az ESP újraindul és csatlakozik a megadott hálózathoz

> Az AP mód addig aktív marad, amíg be nem küldöd az űrlapot – nincs időkorlát.
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

| Ciklus index | Teszt típus |
|:---:|---|
| 0 | HTTP – `msftconnecttest.com/connecttest.txt` |
| 1 | Ping – Cloudflare `1.1.1.1` (4 ping, min. 2 sikeres kell) |
| 2 | HTTP – `msftncsi.com/ncsi.txt` |
| 3 | Ping – Google `8.8.8.8` |
| 4 | HTTP – `msftncsi.com/ncsi.txt` |
| 5+ | HTTP – `msftconnecttest.com/connecttest.txt` |

> A két ping teszt szándékosan más-más szolgáltatót céloz (Cloudflare, ill.
> Google), így az egyikük kiesése nem tűnik internetkimaradásnak.
> A ping teszt akkor áll le, amint az eredmény eldőlt (2 sikeres → sikeres,
> vagy már a maradék pingekkel sem érhető el a 2 → sikertelen), így általában
> a 4 pingnél kevesebbet futtat.
> A HTTP teszt a válaszból legfeljebb 96 bájtot olvas be, és levágja a záró
> sortörést az összehasonlítás előtt.

#### Router reset folyamat

1. Ha **3+ sikertelen teszt** és **cycleIndex > 3**:
   - Relé **bekapcsol** → router áramtalanítva
   - **90 másodperc** várakozás (RESET_PULSE)
   - Relé **kikapcsol** → router visszakap áramot
   - **10 perc** várakozás (RESET_DELAY) – idő a router bootolásához
   - Wi-Fi újracsatlakozás
2. Ha **5 sikertelen reset ciklus** → ESP deep sleep módba lép (1 óra)

### 3. Fizikai gombok

| Gomb | Funkció |
|------|---------|
| **Reset** (D1) | ESP32-C3 azonnali újraindítása; deep sleepből felébreszti az eszközt |
| **Wi-Fi Reset** (D0) | Mentett Wi-Fi adatok törlése + ESP újraindítás → visszaáll AP módba |

> ⚠️ Ha induláskor valamelyik gomb beragadva marad, az ESP 60 másodpercre deep sleep módba lép (védelem).
> Ilyenkor a gombos ébresztés **nincs** bekapcsolva, különben a beragadt gomb végtelen boot loopot okozna.

### Deep sleep és ébredés

| | |
|---|---|
| **Elalvás oka** | 5 sikertelen router reset, vagy sikertelen Wi-Fi újracsatlakozás |
| **Alvás hossza** | 1 óra (timer), vagy 60 mp beragadt gomb esetén |
| **Ébresztés** | timer **vagy** a reset gomb (D1) lenyomása |
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
| `ESPping` | ICMP ping tesztek |

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
| `maxfailureEvents` | 5 | Ennyi reset után deep sleep |
| `wifi_maxRetries` | 3 | Wi-Fi újracsatlakozási próbálkozások |

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
✅ Ping teszt sikeres.
Successful Test
SUCCESS_DELAY delay start.
```

## 🔒 Biztonság

- A LittleFS-en tárolt Wi-Fi adatok (`/ssid.txt`, `/pass.txt`, `/ip.txt`,
  `/gateway.txt`) **nem érhetők el a webszerveren keresztül** – a beállító
  portál csak a `/`, `/style.css` és `/favicon.png` útvonalakat szolgálja ki.
- A jelszó soha nem kerül ki nyílt szövegként a soros portra, csak a hossza.
- Az AP jelszava (`bazsi1234`) a forráskódban van; éles használat előtt
  érdemes lecserélni az `AP_PASSWORD` konstansban.

## 📄 Licenc

MIT License – lásd a [LICENSE](LICENSE) fájlt.
