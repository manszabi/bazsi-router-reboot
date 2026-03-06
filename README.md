# 🔄 Bazsi Router Reboot

ESP32-C3 alapú automatikus router újraindító rendszer. Az eszköz folyamatosan figyeli az internetkapcsolatot, és ha kiesést észlel, relén keresztül automatikusan újraindítja a routert.

## 📋 Jellemzők

- **Automatikus internetkapcsolat-figyelés** – HTTP és ICMP (ping) tesztek váltakozásával
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
| `D1` | Reset / ébresztő gomb (INPUT_PULLUP) |
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
   - **SSID** – a Wi-Fi hálózat neve
   - **Password** – a Wi-Fi jelszó
   - **IP Address** – az ESP kívánt statikus IP-je (opcionális, ha üres → DHCP)
   - **Gateway** – a router IP-je (opcionális, ha üres → DHCP)
5. Küldés után az ESP újraindul és csatlakozik a megadott hálózathoz

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
              │ + 6 perc     │
              │   várakozás  │
              └──────────────┘
```

#### Tesztelési módszerek (ciklikusan váltakoznak)

| Ciklus index | Teszt típus |
|:---:|---|
| 0 | HTTP – `msftconnecttest.com/connecttest.txt` |
| 1 | Ping – Cloudflare `1.1.1.1` (4 ping, min. 2 sikeres kell) |
| 2 | HTTP – `msftncsi.com/ncsi.txt` |
| 3 | Ping – Cloudflare `1.1.1.1` |
| 4 | HTTP – `msftncsi.com/ncsi.txt` |
| 5+ | HTTP – `msftconnecttest.com/connecttest.txt` |

#### Router reset folyamat

1. Ha **3+ sikertelen teszt** és **cycleIndex > 3**:
   - Relé **bekapcsol** → router áramtalanítva
   - **90 másodperc** várakozás (RESET_PULSE)
   - Relé **kikapcsol** → router visszakap áramot
   - **6 perc** várakozás (RESET_DELAY) – idő a router bootolásához
   - Wi-Fi újracsatlakozás
2. Ha **5 sikertelen reset ciklus** → ESP deep sleep módba lép (1 óra)

### 3. Fizikai gombok

| Gomb | Funkció |
|------|---------|
| **Reset** (D1) | ESP32-C3 azonnali újraindítása |
| **Wi-Fi Reset** (D0) | Mentett Wi-Fi adatok törlése + ESP újraindítás → visszaáll AP módba |

> ⚠️ Ha induláskor valamelyik gomb beragadva marad, az ESP 60 másodpercre deep sleep módba lép (védelem).

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
| `RESET_DELAY` | 6 perc | Várakozás a router reset után (bootolási idő) |
| `firstStartDelay` | 3 perc | Első indítás utáni várakozás |
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

## 📄 Licenc

MIT License – lásd a [LICENSE](LICENSE) fájlt.
