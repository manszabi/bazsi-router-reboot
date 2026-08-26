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
| Beragadt gomb ellenőrzés (**mindkettő**: `D0` és `D1`) | azonnal | Ha bármelyik nyomva: LED-ek **felváltva** villognak 3 mp-ig, majd **60 mp** deep sleep |
| Watchdog reset számláló ellenőrzés | azonnal | 3. rendellenes reset → `MODE_FATAL` |
| LittleFS csatolás | azonnal | Hiba → `MODE_FATAL` |
| Konfiguráció beolvasása | azonnal | Olvasási hiba → `MODE_FATAL` |
| Wi-Fi csatlakozás | max. **20 mp** | siker → `MODE_MONITOR` |
| Watchdog bekapcsolása | azonnal | |

### Ha a csatlakozás nem sikerül

| Van mentett SSID? | Viselkedés |
|---|---|
| **Nincs** | Azonnal `MODE_CONFIG` |
| **Van** | **10 perc** várakozás (`firstStartDelay`), majd **3 próba** (köztük 30 mp) |

A 3 próba után a **hiba oka** dönt, nem az eltelt idő:

| `WiFi.status()` | Jelentés | Következmény |
|---|---|---|
| `WL_CONNECT_FAILED` | hitelesítési hiba → rossz jelszó | **Azonnal AP mód** |
| bármi más | a hálózat nem látszik → a router lekapcsolva | **1 órás alvás, majd új kör** |

Az ESP32 core megkülönbözteti a kettőt (`STA.cpp`): `WIFI_REASON_NO_AP_FOUND` →
`WL_NO_SSID_AVAIL`, `WIFI_REASON_AUTH_FAIL` → `WL_CONNECT_FAILED`. Konzervatívan
döntünk: **csak** az explicit hitelesítési hiba küld AP módba, mert a téves
„várjunk tovább" ára késleltetett újrakonfigurálás, a téves „AP mód" ára viszont
egy halott eszköz.

### Meddig próbálkozik? – közel 2 nap

Egy kör **tartalmaz egy router újraindítást** is: ha a Wi-Fi nem látszik, a
router is lehet lefagyva – pontosan ez az eszköz dolga.

```
indulás → 10,0 perc  firstStartDelay várakozás
        →  2,0 perc  3 csatlakozási próba (20 mp timeout, 30 mp szünet)
        →  1,5 perc  ROUTER ÚJRAINDÍTÁS (relé ki, RESET_PULSE)
        → 10,0 perc  várakozás a router bootolására (RESET_DELAY)
        →  2,0 perc  újabb 3 csatlakozási próba
        → 60,0 perc  deep sleep
        = 85,5 perc / kör
```

| | |
|---|---|
| Egy kör | **85,5 perc** (ebből 25,5 perc ébren) |
| Körök száma | `MAX_RETRY_ROUNDS = 33` |
| Összesen | 33 × 85,5 = 2821,5 perc = **47,0 óra** |

34 kör már 48,5 óra lenne, tehát 33 az utolsó, ami két napon belül marad.
Két nap alatt így legfeljebb **33 router-újraindítás** történik.

> **Rossz jelszó esetén nincs router reset.** Ha a `WiFi.status()`
> `WL_CONNECT_FAILED`-et ad, az eszköz azonnal AP módba megy – a router
> áramtalanítása ilyenkor értelmetlen lenne.

Két nap után AP beállító mód, majd 5 perc után alvás. A körszámláló
`RTC_DATA_ATTR`-ben van: a deep sleepet túléli, de **bekapcsolásra és a reset
gombra nullázódik** – kézi beavatkozás mindig friss 2 napos ablakot ad. Sikeres
csatlakozás szintén nullázza.

A 10 perces várakozás oka: áramszünet után az ESP másodpercek alatt elindul,
a router viszont percekig bootol.

**Teljes idő bekapcsolástól az AP módig:** kb. 20 mp + 10 perc + ~2 perc ≈ **12,5 perc**

### Mekkora áramszünetet él túl felügyelet nélkül?

Teszttel kimérve (`PO1`–`PO3`):

| A router ennyi idő múlva áll fel | Eredmény |
|---|---|
| 8 perc | Kivárja az első körben, csatlakozik |
| 11 perc | Az első kör első próbálkozási ablakában elkapja |
| 20 perc | A router reset utáni második ablak elkapja – **még az első körben** |
| ennél tovább | Óránként új kör, mindegyikben egy router újraindítással |
| **~47 óránál tovább** | Feladja: AP mód, majd 5 perc után alvás |

Egy kör két próbálkozási ablakot ad (a router reset előtt és után), így az első
kör önmagában ~25 percet fed le. Ami ebből kimarad, azt a következő körök
kapják el. A tényleges határ **~47 óra**.

## 12. Diagnosztikai napló

Az eszköz az utolsó **32 eseményt** RTC memóriában tárolja, és a beállító
portál `/log` oldalán kiírja. Soros kábel nélkül is megtudható, mi történt.

| Túléli? | |
|---|:---:|
| Deep sleep | igen |
| Watchdog / panic reset | igen |
| Reset gomb | igen |
| **Áramtalanítás** | **nem** |

`RTC_NOINIT_ATTR`-ben van, ezért éli túl a resetet is – pont azokat a hibákat,
amiket ki akarunk vizsgálni. Mérete 264 bájt a C3 ~8 KB-os RTC memóriájából.

Rögzített események: `BOOT` (a reset okával), `WIFI OK`, `WIFI LOST`,
`TEST FAIL`, `ROUTER RESET`, `AP MODE`, `CONFIG SAVED`, `SLEEP`, `FATAL`,
`WDT RESET` – mindegyik uptime bélyeggel és egy paraméterrel (pl. a hiba oka
vagy a sorszám).

A `/log` oldal az aktuális állapotot is mutatja: reset ok, watchdog számláló,
újrapróbálkozási körök, uptime.

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

### Beragadt gomb induláskor

Induláskor **mindkét** gombot ellenőrizzük. Ha bármelyik nyomva van:

1. A két LED **felváltva** villog **3 másodpercig** (ellentétes fázisban)
2. Az eszköz **60 mp** deep sleepbe megy
3. **Gombos ébresztés nélkül** – különben a beragadt gomb azonnal
   újraébresztené, és végtelen boot loop lenne
4. Az esemény bekerül a diagnosztikai naplóba (`STUCK BUTTON`, a paraméter
   megmondja melyik gomb)

> A **felváltva** villogás szándékosan más, mint a végzetes hiba jelzése, ahol a
> két LED **együtt** villog. Ránézésre megkülönböztethető.

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
| **együtt villog 5 Hz** | **együtt villog 5 Hz** | **Végzetes hiba** (LittleFS / konfig / watchdog) |
| **felváltva villog 5 Hz** | **felváltva villog 5 Hz** | **Beragadt gomb** induláskor |
