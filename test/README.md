# Tesztek

Ezek a tesztek a **tényleges `bazsi_router_reboot.ino` fájlt** fordítják le
host gcc-vel, stub Arduino/ESP32 API-k felett. Nem kell hozzájuk ESP32
hardver, sem Arduino toolchain – csak egy C++17-es fordító.

```bash
cd test
make test
```

A suite **kétszer** fut: az ESP32 Arduino 3.3.11 által használt **IDF 5**-ös
ágon, és az **IDF 6**-os ágon is – a deep sleep gomb-ébresztés API-ját ugyanis
az IDF 6 átnevezte, és a sketch mindkét nevet kezeli.

A memóriahibákat és a definiálatlan viselkedést külön cél fogja meg – ugyanaz
a forgatókönyv-halmaz, ASan + UBSan alatt:

```bash
make san
```

A sorlefedettség megmutatja, mely ágakat **nem futtatja egyetlen forgatókönyv
sem** – oda ugyanis semmi nem néz rá:

```bash
make cov
```

Egyetlen forgatókönyv futtatása név-előtag alapján (a bináris a `make` után áll elő):

```bash
./build/run-idf5 W3     # csak a "W3: statikus IP ..." eset
./build/run-idf5 S      # minden sleep teszt
./build/run-idf6 SN     # ugyanaz az IDF 6-os ágon
```

## Hogyan működik

- A `stubs/` a sketch által használt API-k minimális mása. A szignatúrák az
  **ESP32 Arduino 3.3.11** (beágyazott ESP-IDF `release_v5.5`) forrásából
  származnak – pl. `typedef NetworkClient WiFiClient`, `size_t File::read(uint8_t*, size_t)`,
  `HTTPClient::setTimeout(uint16_t)`, `Ping.ping(IPAddress, int16_t)`.
- A `Ping.ping()` és a `http.GET()` a **valódi timeoutjukig "blokkol"** a szimulált
  időben (1 mp, illetve 15 mp), etetés nélkül – enélkül a watchdog etetési
  közének mérése semmit nem érne.
- A pin-számok a valódi `variants/XIAO_ESP32C3/pins_arduino.h`-ból jönnek
  (`D0`=GPIO2, `D1`=GPIO3, `D3`=GPIO5, `D4`=GPIO6, `D5`=GPIO7).
- A stub `millis()`-e **`uint32_t`**, nem `unsigned long`: a hoston az utóbbi 64 bites
  lenne, és akkor a `millis() - start` körbefordulás-biztos idiómák másképp
  viselkednének, mint az ESP32-C3-on (ahol `unsigned long` = 32 bit).
- Az idő szimulált: minden `yield()` 10 ms-ot léptet, így a percekben mérhető
  időzítések ezredmásodpercek alatt tesztelhetők.
- `ESP.restart()` és `esp_deep_sleep_start()` C++ kivételt dob, így a
  visszatérés nélküli útvonalak is ellenőrizhetők.
- **Minden forgatókönyv külön processzben fut** (`fork()`), mert a sketch
  globális állapota (`testState`, `timing`, `uiFlags`) egyébként átszivárogna
  az esetek között. Ez egyben hű is a valósághoz: minden eset hidegindítás.

## Lefedett esetek

**165 forgatókönyv, 534 ellenőrzés. Sorlefedettség: 98,14%.**

| | |
|---|---|
| `W1`–`W9` | Wi-Fi: konfig portál, DHCP vs. statikus IP, DNS, timeout, újracsatlakozás, netif-elvesztés |
| `S1`–`S4` | Deep sleep: beragadt gomb, kimenetek állapota alvás előtt, ébresztőforrások, RTC-láb megkötés |
| `RL1`–`RL2` | Relé: a 90 másodperces reset pulzus hossza, elalvás N reset után |
| `H1`–`H8` | HTTP teszt: CR/LF vágás, hibás státusz, captive portal, chunked válasz, beragadt szerver, **204-es ellenőrzés**, az eszkaláció végpont-sorrendje |
| `PG1`–`PG2` | Ping teszt: 2-a-4-ből szabály és korai kilépés |
| `C1` | Konfig fájlok írása/olvasása, csonkítás, hiányzó fájl |
| `B1` | Gomb debounce: zajtüske vs. tartós nyomás |
| `F1`–`F4` | Webszerver: tartalék űrlap **feltöltetlen `data/` mellett**, feltöltött `data/`, hiányzó fájlok, 404, AP-határidő kitolása |
| `WDT1`–`WDT8` | Watchdog: konfiguráció, etetés a hosszú blokkolások alatt, `delay()` vs. CPU-pörgetés, a feliratkozás tényleges ellenőrzése |
| `SN1`–`SN2` | Biztonsági háló: ha a gomb-ébresztés armolása hibázik, időzítő |
| `FS1`–`FS10` | LittleFS hibák: csatolás, írásvédettség, megtelt tár, **néma írási hiba**, csonka olvasás, törlés tartalék útvonala |
| `FT1`–`FT8` | Végzetes hiba: betölthetetlen konfig vs. „nincs még konfig", LED-villogás, gombok |
| `WF1`–`WF6` | Csatlakozási hiba: újrapróbálkozás vs. AP portál, RTC-számláló |
| `WD1`–`WD9` | Ismétlődő watchdog újraindulás; és a leghosszabb etetés nélküli szakasz minden üzemmódban |
| `E1`–`E6` | Végponttól végpontig: egészséges ciklus, router reset, AP mód, gombok, visszatérő WiFi |
| `P1`–`P15` | Beállító portál mentése: validáció, írási hiba, jelszó-szivárgás, határidő, IP+gateway páros, whitespace, mentés közbeni gombnyomás, halasztott újraindítás |
| `CPU1`–`CPU2` | A loop() nem pörgeti a CPU-t a várakozó állapotokban |
| `X1`–`X14` | Határesetek: gomb a relé pulzus közben, nyílt hálózat, SSID/jelszó határértékek, wifireset törlési sorrend és **sikertelen törlés → végzetes hiba**, query-paraméter, 1 napnál hosszabb uptime |
| `PO1`–`PO3` | Áramszünet: mekkora router-indulási késést tolerál |
| `R1`–`R4` | Újrapróbálkozási politika: rossz jelszó vs. hiányzó hálózat, 2 napos határ |
| `L1`–`L7` | Diagnosztikai napló: rögzítés, /log oldal, körpuffer, spam-védelem, esemény-kódok, minden címke, üres napló |
| `SB1`–`SB4` | Beragadt gomb: mindkét gomb, váltakozó LED-villogás, naplózás |
| `SER1`–`SER3` | Soros kimenet terhelése: nem árasztja el a konzolt |
| `OV1` | Számlálók korlátosak több reset cikluson át |
| `IP1`–`IP3` | Csak IPv4 fogadható el (IPv6 és `0.0.0.0` nem) |
| `LED1` | A router áramtalanításakor egyik LED sem hazudik |

A név-előtag **prefix**, nem pontos egyezés: a `P1` így a `P1`, `P10`–`P15`
eseteket is futtatja.

### Ami szándékosan fedetlen

A `make cov` hét sort jelez, mindegyik védekező vagy bizonyíthatóan
elérhetetlen – ezeket **nem** kell tesztelni:

| Hol | Miért nem érhető el |
|---|---|
| `eventName()` `default` ága | minden létező eseménykódnak van címkéje (az `L6` ezt ellenőrzi) |
| `readConfigValue()` `file.close()` a könyvtár-ágon | a stub nem tud könyvtárat adni |
| `readBounded()` két `break`-je | védekező ág arra, ha az `available()` hazudik |
| `testInternetPing()` záró `return`-je | a ciklus minden ága korábban visszatér (az utolsó körben `remaining == 0`) |
| POST `!fsReady` ága | ha a LittleFS nem csatolható, a `setup()` `MODE_FATAL`-ba megy, és a portál el sem indul |
| A jelszókódolás puffer-hiba ága | a POST már ellenőrzi a hosszt, a puffer pedig pontosan 63 karakterre méretezett – nem tud elbukni |
| `FAILURE_STATE` záró `break`-je | előtte `wifiGiveUp()` vagy módváltás történik |

## Fontos

A stubok **nem** emulálják az ESP32-t. Azt ellenőrzik, hogy a firmware
vezérlési logikája, időzítései és API-használata helyes-e. A valódi rádió,
lwIP, LittleFS és a relé viselkedését hardveren kell igazolni.
