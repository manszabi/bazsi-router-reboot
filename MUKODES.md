# Működési táblázat

Az eszköz teljes viselkedése: mikor mit csinál és meddig. Minden érték a
`bazsi_router_reboot.ino` konstansaiból származik.

---

## 1. Üzemmódok

Az eszköz mindig pontosan egy üzemmódban van.

| Üzemmód | Mikor | Mit csinál |
|---|---|---|
| `MODE_MONITOR` | Van Wi-Fi kapcsolat, vagy épp épül | Internetet tesztel, szükség esetén routert indít újra |
| `MODE_CONFIG` | Nincs használható Wi-Fi konfiguráció | AP beállító portál (`ESP-<chip>` / `bazsi1234`, `192.168.4.1`) |
| `MODE_FATAL` | Betölthetetlen konfiguráció, vagy 3 watchdog reset | Mindkét LED villog, semmi más nem fut |

---

## 2. Bekapcsolás → első kapcsolat

| Lépés | Időtartam | Következő |
|---|---|---|
| Kimenetek alapállapotba (relé `LOW`, státusz LED be) | azonnal | |
| Soros port indítása | max. **3 mp** + 0,5 mp | |
| Beragadt gomb ellenőrzés | azonnal | Ha nyomva: **60 mp** deep sleep, majd újra |
| Watchdog reset számláló ellenőrzés | azonnal | 3. rendellenes reset → `MODE_FATAL` |
| LittleFS csatolás | azonnal | Hiba → `MODE_FATAL` |
| Konfiguráció beolvasása | azonnal | Olvasási hiba → `MODE_FATAL` |
| Wi-Fi csatlakozás | max. **20 mp** | siker → `MODE_MONITOR` |
| Watchdog bekapcsolása | azonnal | |

### Ha a csatlakozás nem sikerül

| Van mentett SSID? | Viselkedés |
|---|---|
| **Nincs** | Azonnal `MODE_CONFIG` |
| **Van** | **10 perc** várakozás (`firstStartDelay`), majd **3 próba** (köztük 30 mp), és csak utána `MODE_CONFIG` |

A 10 perces várakozás oka: áramszünet után az ESP másodpercek alatt elindul,
a router viszont percekig bootol.

**Teljes idő bekapcsolástól az AP módig:** kb. 20 mp + 10 perc + ~2 perc ≈ **12,5 perc**

### Mekkora áramszünetet él túl felügyelet nélkül?

Teszttel kimérve (`PO1`–`PO3`):

| A router ennyi idő múlva áll fel | Eredmény |
|---|---|
| 8 perc | Kivárja, csatlakozik, **normálisan működik** |
| 11 perc | Az újrapróbálkozási ablakban még **elkapja** |
| 20 perc | **Túl késő** → AP mód, majd 5 perc után alvás |

A határ nagyjából **12 perc**. Ennél lassabban induló router (DSL újraszinkron,
hosszabb szolgáltatói kimaradás) esetén az eszköz AP módba parkol, és onnan
csak a reset gombbal hozható vissza.

---

## 3. Normál működés – az internet figyelése

Állapotgép három állapottal.

| Állapot | Mit csinál | Meddig |
|---|---|---|
| `TESTING_STATE` | Lefuttat egy internet tesztet | 1–15 mp (teszttípustól függ) |
| `SUCCESS_STATE` | Vár a következő tesztig | **1 perc** (`SUCCESS_DELAY`) |
| `FAILURE_STATE` | Vár az újratesztelésig | **12 mp** (`PROBE_DELAY`) |

### A tesztek ciklikusan váltakoznak

| Ciklus | Teszt | Maximális időtartam |
|:---:|---|---|
| 0 | HTTP – `msftconnecttest.com` | ~15 mp (5 mp connect + 10 mp válasz) |
| 1 | Ping – Cloudflare `1.1.1.1` | ~5 mp (max. 3 ping × 1 mp + szünetek) |
| 2 | HTTP – `msftncsi.com` | ~15 mp |
| 3 | Ping – Google `8.8.8.8` | ~5 mp |
| 4 | HTTP – `msftncsi.com` | ~15 mp |
| 5+ | HTTP – `msftconnecttest.com` | ~15 mp |

Sikeres teszt → minden számláló nullázódik, a ciklus 0-ról indul.

A ping teszt **4 pingből legalább 2 sikert** vár, de leáll, amint az eredmény
eldőlt (tehát általában 2–3 pinget futtat).

---

## 4. Az internet kiesik – router újraindítás

Akkor indul, ha **3 sikertelen teszt** és a **ciklus index > 3** (kb. 4 kör).

| Lépés | Időtartam |
|---|---|
| Relé **be** → router áramtalanítva, státusz LED ki | |
| Áramtalanítás | **90 mp** (`RESET_PULSE`) |
| Relé **ki** → router visszakap áramot, státusz LED be | |
| Várakozás a router bootolására | **10 perc** (`RESET_DELAY`) |
| Wi-Fi újracsatlakozás: 3 próba, köztük 30 mp | max. ~2 perc |

**Egy teljes reset ciklus:** ~4 kör teszt (~1 perc) + 90 mp + 10 perc + ~2 perc ≈ **14 perc**

### Ha az újracsatlakozás sem sikerül

`MODE_CONFIG` (AP beállító portál).

### Ha az internet tartósan nem jön vissza

| Reset sorszám | Következmény |
|:---:|---|
| 1–4 | Lefut a router újraindítás |
| **5. esemény** | Már nem indítja újra a routert: **deep sleep 1 órára**, majd magától ébred és újrapróbálja |

Tehát **4 tényleges router újraindítás** történik: a `maxfailureEvents = 5`
számláló még a relé kapcsolása előtt nő, és az ötödik eseménynél alszik el.

Ez az egyetlen alvás, ami **magától felébred** – itt a Wi-Fi működik, csak az
internet nem, tehát érdemes később újrapróbálni.

---

## 5. Wi-Fi kapcsolat megszakad működés közben

| Lépés | Időtartam |
|---|---|
| Wi-Fi LED ki | azonnal |
| **3 újracsatlakozási próba**, köztük 30 mp | max. ~2 perc |
| Sikertelen → azonnal **router újraindítás** (4. pont szerint) | |

Nem várunk további teszt ciklusokat: ha nincs Wi-Fi, a tesztnek nincs értelme.

---

## 6. AP beállító mód

| Esemény | Időtartam |
|---|---|
| Portál elérhető: `192.168.4.1` | |
| Tétlenség után deep sleep | **5 perc** (`AP_TIMEOUT_MS`) |
| Az alvásból ébredés | **csak a reset gombbal** – időzített ébresztés nincs |

Két védelem óvja a mentést:

- **Minden HTTP kérés újraindítja az 5 perces visszaszámlálást.** Ha az utolsó
  pillanatban nyitod meg az oldalt, kapsz még egy teljes időablakot.
- **Fájlírás közben az eszköz soha nem alszik el**, tehát nem maradhat félig
  kiírt konfiguráció.

Sikeres mentés után: válasz elküldése, **2 mp** türelmi idő, majd újraindulás.
Sikertelen mentésnél HTTP 500 és **nincs** újraindulás – a beírt adatok maradnak.

---

## 7. Végzetes hiba – `MODE_FATAL`

| Kiváltó ok | |
|---|---|
| A LittleFS nem csatolható | |
| A konfigurációs fájl létezik, de nem olvasható | |
| 3 watchdog/panic miatti újraindulás | |

> A **hiányzó vagy üres** konfiguráció **nem** végzetes hiba – az első indítás
> és a wifireset gomb utáni állapot is ilyen. Ilyenkor AP mód indul.

| Viselkedés | Érték |
|---|---|
| Mindkét LED együtt villog | **100 mp be / 100 mp ki** (5 Hz) |
| Relé | `LOW` – a router végig kap áramot |
| Állapotgép | nem fut |
| Gombok | működnek |
| Deep sleep | **5 perc** után |
| Ébredés | **csak a reset gombbal** |

---

## 8. Watchdog

| Paraméter | Érték |
|---|---|
| Timeout | **90 mp** |
| Etetés a hosszú várakozások alatt | ~10 ms-onként |
| Timeoutkor | panic → újraindulás |
| 3 rendellenes újraindulás után | `MODE_FATAL` |
| A számláló nullázódik | áramtalanításkor, vagy **1 óra** hibátlan működés után |

Rendellenesnek számít: `ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT`, `ESP_RST_WDT`,
`ESP_RST_PANIC`, `ESP_RST_CPU_LOCKUP`.

---

## 9. Gombok

| Gomb | Pin | Hatás | Feltétel |
|---|---|---|---|
| Reset | `D1` = GPIO3 | Azonnali újraindítás; deep sleepből ébreszt | **50 mp** folyamatos nyomás |
| Wi-Fi reset | `D0` = GPIO2 | Mentett adatok törlése + újraindítás | **50 mp** folyamatos nyomás |

Induláskor beragadt gomb → **60 mp** deep sleep (gombos ébresztés nélkül, hogy
ne legyen újraindítási hurok).

---

## 10. Az összes alvás egy helyen

| Kiváltó ok | Hossz | Magától ébred? |
|---|---|:---:|
| Beragadt gomb induláskor | 60 mp | **igen** |
| 5 sikertelen router reset (internet nincs) | 1 óra | **igen** |
| AP mód tétlenség | örökre | **nem** – csak gomb |
| Végzetes hiba (LittleFS / konfig / watchdog) | örökre | **nem** – csak gomb |

Minden alvás előtt a **relé `LOW`**, tehát a router kap áramot.

> ⚠️ Deep sleep alatt az ESP32-C3 `GPIO6–21` lábai nagyimpedanciásak, így a relé
> (`D5` = GPIO7) **lebeg**. Ez szoftverből nem javítható – aktív-HIGH
> relémodulnál külső 10 kΩ lehúzó ellenállás kell a GND felé.

---

## 11. Állapotjelzés a LED-eken

| Státusz LED (`D4`) | Wi-Fi LED (`D3`) | Jelentés |
|---|---|---|
| be | be | Minden rendben, van Wi-Fi |
| be | ki | Nincs Wi-Fi kapcsolat |
| ki | ki | Router áramtalanítva (reset pulzus alatt), vagy alvás |
| **együtt villog 5 Hz** | **együtt villog 5 Hz** | **Végzetes hiba** |
