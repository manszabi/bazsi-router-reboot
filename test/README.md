# Tesztek

Ezek a tesztek a **tényleges `bazsi_router_reboot.ino` fájlt** fordítják le
host gcc-vel, stub Arduino/ESP32 API-k felett. Nem kell hozzájuk ESP32
hardver, sem Arduino toolchain – csak egy C++17-es fordító.

```bash
cd test
make test
```

Egyetlen forgatókönyv futtatása név-előtag alapján:

```bash
./build/run W3      # csak a "W3: statikus IP ..." eset
./build/run S       # minden sleep teszt
```

## Hogyan működik

- A `stubs/` a sketch által használt API-k minimális mása. A szignatúrák az
  **ESP32 Arduino 3.3.11** (beágyazott ESP-IDF `release_v5.5`) forrásából
  származnak – pl. `typedef NetworkClient WiFiClient`, `size_t File::read(uint8_t*, size_t)`,
  `HTTPClient::setTimeout(uint16_t)`, `Ping.ping(IPAddress, int16_t)`.
- A pin-számok a valódi `variants/XIAO_ESP32C3/pins_arduino.h`-ból jönnek
  (`D0`=GPIO2, `D1`=GPIO3, `D3`=GPIO5, `D4`=GPIO6, `D5`=GPIO7).
- Az idő szimulált: minden `yield()` 10 ms-ot léptet, így a percekben mérhető
  időzítések ezredmásodpercek alatt tesztelhetők.
- `ESP.restart()` és `esp_deep_sleep_start()` C++ kivételt dob, így a
  visszatérés nélküli útvonalak is ellenőrizhetők.
- **Minden forgatókönyv külön processzben fut** (`fork()`), mert a sketch
  globális állapota (`testState`, `timing`, `uiFlags`) egyébként átszivárogna
  az esetek között. Ez egyben hű is a valósághoz: minden eset hidegindítás.

## Lefedett esetek

| | |
|---|---|
| `W1`–`W9` | Wi-Fi: konfig portál, DHCP vs. statikus IP, DNS, timeout, újracsatlakozás, netif-elvesztés |
| `S1`–`S4` | Deep sleep: beragadt gomb, kimenetek állapota alvás előtt, ébresztőforrások, RTC-láb megkötés |
| `R1`–`R2` | Relé: a 90 másodperces reset pulzus hossza, elalvás N reset után |
| `H1`–`H4` | HTTP teszt: CR/LF vágás, hibás státusz, captive portal, chunked válasz |
| `P1`–`P2` | Ping teszt: 2-a-4-ből szabály és korai kilépés |
| `C1` | Konfig fájlok írása/olvasása, csonkítás, hiányzó fájl |
| `B1` | Gomb debounce: zajtüske vs. tartós nyomás |
| `F1` | Tartalék beállító űrlap, ha a `data/` mappa nincs feltöltve |
| `SB1`–`SB4` | Beragadt gomb: mindkét gomb, váltakozó LED-villogás, naplózás |
| `R1`–`R4` | Újrapróbálkozási politika: rossz jelszó vs. hiányzó hálózat, 2 napos határ |
| `L1`–`L4` | Diagnosztikai napló: rögzítés, /log oldal, körpuffer, spam-védelem |
| `PO1`–`PO3` | Áramszünet: mekkora router-indulási késést tolerál |
| `X1`–`X6` | Határesetek: gomb a relé pulzus közben, nyílt hálózat, SSID/jelszó határértékek |
| `CPU1`–`CPU2` | A loop() nem pörgeti a CPU-t a várakozó állapotokban |
| `P1`–`P7` | Beállító portál mentése: validáció, írási hiba, jelszó-szivárgás, határidő |
| `E1`–`E5` | Végponttól végpontig: egészséges ciklus, router reset, AP mód, gombok |
| `WF1`–`WF5` | Csatlakozási hiba: újrapróbálkozás vs. AP portál, RTC-számláló |
| `FS1`–`FS6` | LittleFS hibák: csatolás, írásvédettség, megtelt tár, sérült tartalom, törlés tartalék útvonala |
| `FT1`–`FT6` | Végzetes hiba: betölthetetlen konfig vs. „nincs még konfig", LED-villogás, gombok |
| `WD1`–`WD6` | Ismétlődő watchdog újraindulás: számlálás, végzetes leállás, nullázási feltételek |
| `WDT1`–`WDT4` | Watchdog: konfiguráció, etetés a hosszú blokkolások alatt, `delay()` vs. CPU-pörgetés |

## Fontos

A stubok **nem** emulálják az ESP32-t. Azt ellenőrzik, hogy a firmware
vezérlési logikája, időzítései és API-használata helyes-e. A valódi rádió,
lwIP, LittleFS és a relé viselkedését hardveren kell igazolni.
