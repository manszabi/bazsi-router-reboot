# Működési táblázat

Az eszköz teljes viselkedése: mikor mit csinál és meddig. Minden érték a
`bazsi_router_reboot.ino` konstansaiból származik.

---

## 1. Üzemmódok

Az eszköz mindig pontosan egy üzemmódban van.

| Üzemmód | Mikor | Mit csinál |
|---|---|---|
| `MODE_MONITOR` | Van Wi-Fi kapcsolat, vagy épp épül | Internetet tesztel, szükség esetén routert indít újra |
| `MODE_CONFIG` | Nincs használható Wi-Fi konfiguráció | AP beállító portál (`ESP-<chip>` / `12345678`, `192.168.4.1`) |
| `MODE_FATAL` | A fájlrendszer nem használható, vagy 3 watchdog reset | Mindkét LED villog, semmi más nem fut |

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
| **Van** | **legfeljebb 10 perc** várakozás (`firstStartDelay`), majd **3 próba** (köztük 30 mp). A várakozás korábban véget ér, ha a 60 mp-enkénti próbánál a hálózat **és** az internet is megvan – lásd lent |

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

> A két 10 perces tétel **felső korlát**. A firmware 60 mp-enként megnézi, hogy
> visszajött-e a hálózat és az internet, és ha igen, azonnal továbblép. Erre a
> táblázatra ez mégsem hat: ha a hálózat végig halott – márpedig ez a szakasz
> pontosan arról szól –, a próbák sosem sikerülnek, és a körök a fenti teljes
> hosszukban futnak. A 46 órás türelem tehát változatlan.

| | |
|---|---|
| Egy kör | **85,5 perc** (ebből 25,5 perc ébren) |
| Körök száma | `MAX_RETRY_ROUNDS = 33` |
| Összesen | 33 × 25,5 + 32 × 60 = 2761,5 perc = **46,0 óra** |

Az utolsó kör **nem alszik egyet**: a `wifiGiveUp()` előbb növeli a számlálót,
aztán ellenőrzi, tehát a 33.-nál már AP módba megy. Ezért 33 × 85,5 helyett
33 × 25,5 + 32 × 60 a helyes összeg. Két napon belül marad, tartalékkal is:
34 kör is csak 47,5 óra lenne. Két nap alatt legfeljebb **33
router-újraindítás** történik. Az `R8` teszt végigjátssza mind a 33 kört.

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
| **~46 óránál tovább** | Feladja: AP mód, majd 5 perc után alvás |

**A két hosszú várakozás 60 mp-enkénti próbája** (`onlineProbe()`) két lépésből
áll, ebben a sorrendben:

1. **Wi-Fi – kísérlet, nem teszt.** Kapcsolat híján csak *kezdeményez* egyet
   (aszinkron `WiFi.begin()`, nem vár rá), és kilép. A kudarcnak **nincs
   következménye**: nem nő számláló, nem változik állapot. A „3 próba, aztán
   router reset" eszkaláció ettől független, az a várakozás *lejárta után* jön.
2. **Internet – csak meglévő kapcsolat mellett.** Ping a `1.1.1.1`-re. Hálózat
   nélkül el sem indul, mert értelmetlen lenne.

A várakozás csak akkor zárul korábban, ha **mindkét lépés** sikerül.

Egy kör két próbálkozási ablakot ad (a router reset előtt és után), így az első
kör önmagában ~25 percet fed le. Ami ebből kimarad, azt a következő körök
kapják el. A tényleges határ **46,0 óra** (mérve: `R8`).

---

## 3. Normál működés – az internet figyelése

Állapotgép három állapottal.

| Állapot | Mit csinál | Meddig |
|---|---|---|
| `TESTING_STATE` | Lefuttat egy internet tesztet | 1–33 mp (a névfeloldástól függ) |
| `SUCCESS_STATE` | Vár a következő tesztig | **1 perc** (`SUCCESS_DELAY`) |
| `FAILURE_STATE` | Vár az újratesztelésig | **12 mp** (`PROBE_DELAY`) |

### A tesztek ciklikusan váltakoznak

Mind az öt teszt HTTP, öt különböző üzemeltető felé. **Nincs internet = mind
az öt elbukik**; bármelyik siker nullázza a számlálókat.

| Kiírt sorszám | `cycleIndex` | Végpont | Üzemeltető | Elvárás | Max. időtartam |
|:---:|:---:|---|---|---|---|
| 1 | 0 | `msftconnecttest.com/connecttest.txt` | Microsoft | `Microsoft Connect Test` | 15 / 33 mp |
| 2 | 1 | `cp.cloudflare.com/generate_204` | Cloudflare | 204 | 15 / 33 mp |
| 3 | 2 | `detectportal.firefox.com/success.txt` | Mozilla | `success` | 15 / 33 mp |
| 4 | 3 | `nmcheck.gnome.org/check_network_status.txt` | GNOME | `NetworkManager is online` | 15 / 33 mp |
| 5 | 4 | `connectivitycheck.gstatic.com/generate_204` | Google | 204 | 15 / 33 mp |

A soros port és a `/log` oldal a **kiírt sorszámmal** dolgozik (1–5); a
`cycleIndex` változó marad 0-alapú, mert ahhoz kötődik a végpontválasztó
`if`-lánc és a `RESET_TRIGGER_CYCLE` küszöb.

A két szám a bukás két módja. **15 mp**, ha a névfeloldás megy, de a szerver
hallgat: 5 mp connect + 10 mp válasz timeout. **33 mp**, ha maga a DNS halott
– a névfeloldásra a `HTTPClient` egyik timeoutja sem vonatkozik, mert a
`NetworkClient::connect()` *előbb* old fel nevet. Részletes levezetés a
README „Miért 90 mp, és nem 30" szakaszában; a `WD13` teszt méri.

A **204-es ellenőrzés** szigorúbb a szöveg-egyeztetésnél: egy captive portál
nem tudja utánozni, mert neki bejelentkező oldalt vagy átirányítást kell
küldenie.

**Ping nincs az internettesztek között**, mert az ICMP nem bizonyít
névfeloldást: befagyott router-DNS mellett a ping megy, az internet mégsem
elérhető. Ping két másik helyen van: a saját gateway ellenőrzésénél (4. pont)
és a hosszú várakozások korai lezárásánál (`onlineProbe()`, 2. és 4. pont).
Egyik sem szavazhat az internettesztben – a korai kilépés után is a teljes
HTTP sorozat dönt.

Sikeres teszt → minden számláló nullázódik, a ciklus újra az 1. végponttal
indul. A soros portra ilyenkor csak a `Successful Test` kerül ki: a
`testInternetHTTP()` siker esetén nem írja ki sem a kapott törzset, sem külön
nyugtázó sort. Eltéréskor viszont igen (a kapott törzs + `Hamis érték!`), mert
ott az a kérdés, mi jött vissza a várt válasz helyett.

---

## 4. Az internet kiesik – router újraindítás

Akkor indul, ha `cycleIndex > 3`, vagyis ha az **5. teszt** (Google,
`cycleIndex == 4`) is elbukott. Mivel a két számláló együtt lép (`failedCount == cycleIndex + 1`),
ez pontosan **5 egymás utáni sikertelen teszt** – de a feltétel szándékosan a
**végpont-lefedettséget** mondja ki, nem az időt: mind az öt üzemeltetőt
végig kell próbálni, hogy egyetlen szolgáltató kiesése soha ne látszódjon
internetkimaradásnak. Az első teszt indulásától a relé
megszólalásáig **123 mp** (2,05 perc), ha a nevek feloldódnak és csak a
szerverek hallgatnak:

```
5 × 15 mp (HTTP timeout)  +  4 × 12 mp (PROBE_DELAY)  =  123 mp
```

Halott DNS mellett – ez a tipikus eset, amit az eszköz javít – minden teszt
33 mp-ig tart, tehát:

```
5 × 33 mp (DNS + connect)  +  4 × 12 mp (PROBE_DELAY)  =  213 mp (3,55 perc)
```

Ha az internet közvetlenül egy sikeres teszt után hal meg, ehhez még hozzájön
a `SUCCESS_DELAY` hátralévő része, tehát a **legrosszabb felismerési idő
60 + 213 = 273 mp ≈ 4,5 perc** (élő DNS mellett 60 + 123 = 183 mp ≈ 3 perc).

| Lépés | Időtartam |
|---|---|
| Relé **be** → router áramtalanítva, státusz LED **villog 2 Hz** | 90 mp sötét LED ránézésre a halott eszköztől sem különbözne |
| Áramtalanítás | **90 mp** (`RESET_PULSE`) |
| Relé **ki** → router visszakap áramot, státusz LED be | |
| Várakozás a router bootolására | **legfeljebb 10 perc** (`RESET_DELAY`) – 60 mp-enként megnézzük, hogy visszajött-e a hálózat és az internet; ha igen, a várakozás azonnal véget ér |
| Wi-Fi újracsatlakozás: 3 próba, köztük 30 mp | max. ~2 perc |

**Egy teljes reset ciklus:** 5 teszt (123–213 mp) + 90 mp + 10 perc +
újracsatlakozás ≈ **13,6–15,1 perc**, ha a Wi-Fi az első próbára visszajön;
ha mind a 3 próba kell, ≈ **15,5–17,0 perc**.

### Ha a saját gateway sem válaszol

Statikus IP mellett külön eset: a Wi-Fi **társítás sikerül** (az WPA2, 2. réteg),
de IP szinten nincs út sehová. Ez jellemzően **elgépelt statikus IP** – és ezt a
router újraindítása soha nem javítja.

Az eszköz ezért a router áramtalanítása előtt megpingeli a saját gateway-ét:

| | |
|---|---|
| A gateway válaszol | a hiba nem helyi → **megszokott viselkedés** (reset, majd újra) |
| Nem válaszol, **először** | a router kap **egy esélyt**: reset, majd újra ellenőrzés (`GW UNREACH` 1) |
| A reset után **sem** válaszol | **AP beállító mód**, hogy javítható legyen (`GW UNREACH` 2, `AP MODE` 4) |
| DHCP-nél | **nincs ellenőrzés** – a gateway magától a routertől jött |

> **Ha a routered nem válaszol ICMP echóra**, ez tévesen „elérhetetlen"-t ad.
> Épp ezért kap a router előbb egy esélyt, és csak másodszorra következik az AP
> mód. Ilyen routernél használj DHCP-t.

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
| Wi-Fi LED **villog 1 Hz**, státusz LED végig **be** | azonnal |
| **3 újracsatlakozási próba**, köztük 30 mp | max. ~2 perc |
| Sikertelen → azonnal **router újraindítás** (4. pont szerint) | |

Nem várunk további teszt ciklusokat: ha nincs Wi-Fi, a tesztnek nincs értelme.

---

## 6. AP beállító mód

| Esemény | Időtartam |
|---|---|
| Portál elérhető: `192.168.4.1` | |
| A beállító űrlap forrása | a programba fordítva (`CONFIG_FORM`), nincs feltöltendő `data/` mappa – a portál a LittleFS-ről semmit nem szolgál ki |
| Tétlenség után deep sleep | **5 perc** (`AP_TIMEOUT_MS`) az utolsó kéréstől |
| A nyitva lévő lap keep-alive-ja | **60 mp**-enként `GET /ping` (1 bájt válasz) |
| Elfogadott IP / gateway | **csak IPv4**, nem `0.0.0.0`, és csak együtt |
| SSID / jelszó | mentéskor a szélső szóközök levágódnak |
| A jelszó tárolása | összekeverve (`v1:` + hexa), az eFuse MAC-hez kötve |
| Az alvásból ébredés | **csak a reset gombbal** – időzített ébresztés nincs |

Négy védelem óvja a mentést:

- **Amíg a lap nyitva van, nem alszik el.** A beállító oldal (és a naplóoldal)
  60 mp-enként meghívja a `/ping` végpontot, ami újraindítja az 5 perces
  visszaszámlálást. Enélkül az óra a *lapbetöltéstől* ketyegne, nem az utolsó
  interakciótól – mérve: 6 percnyi lassú gépelés után a Submit már nem érte el
  az eszközt. Applikációváltás után visszatérve a `visibilitychange` azonnal is
  jelez, nem kell a következő 60 mp-es ütemre várni.

- **Minden HTTP kérés újraindítja az 5 perces visszaszámlálást** – a 404-es
  válasszal végződő is, mert az is interakció. A határidő **abszolút**: mindig
  pontosan 5 perccel a *legutolsó* kérés utánra kerül, nem halmozódik. Felső
  korlát nincs: amíg nézegeted az oldalt, az eszköz ébren marad.
- **Fájlírás közben az eszköz soha nem alszik el**, tehát nem maradhat félig
  kiírt konfiguráció.
- **Fájlírás közben a gombok sem indítanak újra.** A mentést az aszinkron
  webszerver taskja végzi, a gombokat viszont a `loop()` figyeli – egy éppen
  odaeső gombnyomás félbeszakított írást (a wifireset esetén ráadásul egyidejű
  törlést) okozna. A gombok a mentés befejeztével működnek tovább.
- **A sikeres mentés utáni újraindítás is megvárja az írást.** A 2 mp-es
  türelmi idő alatt újabb mentés érkezhet – mobilon a dupla koppintás gyakori –,
  és az újraindítás félbe vágná. Ugyanez igaz **minden alvásra**: az
  `enterDeepSleep()` egyetlen torlópont, amin minden alvás áthalad.

A várakozás **korlátos** (5 mp): ha a jelző bármiért beragadna, az eszköz nem
fagyhat le miatta – továbblép, és a soros porton szól róla.

Sikeres mentés után: válasz elküldése, **2 mp** türelmi idő, majd újraindulás.
A mentés **két fázisú**: előbb minden mező validálódik, és fájl csak akkor
íródik, ha mindegyik érvényes. Sikertelen validálásnál HTTP 500 és **nincs**
újraindulás – sem a fájlok, sem a futó konfiguráció nem változik, a beírt
adatok az űrlapon maradnak. Ha a konfigfájlokat épp más írja (wifireset gombos
törlés), a mentés HTTP 503-mal hátrál.

---

## 7. Végzetes hiba – `MODE_FATAL`

| Kiváltó ok | Napló |
|---|:---:|
| A LittleFS nem csatolható | `FATAL` 1 |
| A konfigurációs fájl létezik, de nem olvasható | `FATAL` 2 |
| 3 watchdog/panic miatti újraindulás | `FATAL` 3 |
| **A wifireset gomb nem tudta törölni a mentett adatokat** | `FATAL` 4 |

> A **hiányzó vagy üres** konfiguráció **nem** végzetes hiba – az első indítás
> és a wifireset gomb utáni állapot is ilyen. Ilyenkor AP mód indul.

Mind a négy ok ugyanaz a hibaosztály: **a fájlrendszer nem használható**, tehát
a konfiguráció sem betölteni, sem menteni nem lehet. Ilyen állapotban az eszköz
nem fut tovább – az újraindítgatás csak elfedné a hibát.

| Viselkedés | Érték |
|---|---|
| Mindkét LED együtt villog | **100 ms be / 100 ms ki** (`FATAL_BLINK_MS`, 5 Hz) |
| Relé | `LOW` – a router végig kap áramot |
| Állapotgép | nem fut |
| Gombok | a reset gomb mindig; a wifireset a 4-es oknál nem (lásd lent) |
| Deep sleep | **5 perc** után (`FATAL_SLEEP_AFTER_MS`) |
| Ébredés | **csak a reset gombbal** vagy áramtalanítással |

### Hol keletkezik a jelzés

A 4-es ok annyiban különbözik, hogy a gombkezelőből jön, ami **blokkoló
ciklusokból is futhat** (a `waitWithButtons(RESET_DELAY)` például 10 percig nem
ad vissza a `loop()`-nak). Ha csak beállítanánk a módot, az eszköz addig tovább
működne – tesztelne, relét kapcsolna. Ezért a jelzés ott helyben, blokkolva
történik, és soha nem tér vissza. Kívülről nézve a viselkedés azonos.

---

## 8. Watchdog

| Paraméter | Érték |
|---|---|
| Timeout | **90 mp** |
| Élesedés | a soros port beállása után **azonnal**, a gombellenőrzés és a LittleFS csatolása **előtt** |
| Etetés nélküli leghosszabb `setup()`-szakasz | a LittleFS formázása: 4–7 mp (rossz esetben ~51 mp), 512 KiB = 128 szektor |
| Leghosszabb etetés nélküli szakasz (mérve) | **33 mp** (egy halott DNS-be futó HTTP kérés; hallgató szerver mellett 15 mp) |
| Etetés a hosszú várakozások alatt | ~10 ms-onként |
| Timeoutkor | panic → újraindulás |
| 3 rendellenes újraindulás után | `MODE_FATAL` |
| A számláló nullázódik | áramtalanításkor, vagy **1 óra** hibátlan működés után |

Rendellenesnek számít: `ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT`, `ESP_RST_WDT`,
`ESP_RST_PANIC`, `ESP_RST_CPU_LOCKUP`.

### Ha a watchdogot nem sikerül élesíteni

Az `enableLoopWDT()` a core-ban `void`: ha a feliratkozás nem sikerül, csak egy
belső hibaüzenet jelzi. Ezért az indulás után visszakérdezünk
(`esp_task_wdt_status()`), és **csak akkor** írjuk ki, hogy `Watchdog enabled`,
ha a `loop()` tényleg figyelve van. Ha nem:

```
FIGYELEM: a watchdog NEM vedi a loop()-ot (...). A program fut, de
lefagyas eseten nem indul ujra.
```

Ilyenkor a firmware **nem etet** – etetés feliratkozás nélkül csak
hibaüzeneteket öntene a soros portra (mérve 100 sor/perc), védelmet nem adna.

---

## 9. Gombok

| Gomb | Pin | Hatás | Feltétel |
|---|---|---|---|
| Reset | `D1` = GPIO3 | Azonnali újraindítás; deep sleepből ébreszt | **50 ms** folyamatos nyomás (`BUTTON_DEBOUNCE_MS`) |
| Wi-Fi reset | `D0` = GPIO2 | Mentett adatok törlése + újraindítás | **50 ms** folyamatos nyomás (`BUTTON_DEBOUNCE_MS`) |

A gombokat 10 ms-onként mintavételezzük, és csak a végig lenyomva maradt
állapotot fogadjuk el – egyetlen zajtüske tehát nem indít újra.

> ⚠️ A Wi-Fi reset gomb lába (`D0` = **GPIO2**) az ESP32-C3 egyik *strapping*
> lába: a chip csak akkor bootol, ha a reset pillanatában GPIO2 = 1.
> **Bekapcsolás közben ne tartsd nyomva** – az eszköz el sem indulna, és ez
> ellen szoftver nem védhet. Új hardver revízióban a gombot érdemes szabad,
> nem-strapping lábra tenni (pl. `D2` = GPIO4).

**Fájlírás közben egyik gomb sem hat.** A mentést az aszinkron webszerver
taskja végzi; egy odaeső gombnyomás félbeszakított írást okozna. A gombok a
mentés befejeztével működnek tovább (ilyenkor egy új 50 ms-os lenyomás kell).

A wifireset a `/ssid.txt`-t törli **először**: a gomb célja, hogy az eszköz a
beállító portálon jöjjön fel, és ezt egyedül ez a fájl dönti el.

**Ha a törlés nem sikerül, az végzetes hiba** (7. fejezet, `FATAL` 4). Ilyenkor
a fájlrendszer nem írható, tehát új konfigurációt sem lehetne menteni – az
újraindítás csak a régi adatokkal hozná vissza az eszközt, a gomb pedig kívülről
nézve „nem csinálna semmit". Az eszköz ezért nem indul újra, hanem jelez.

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

> Biztonsági háló: ha a gombos ébresztés élesítése bármiért hibázna (pl. valaki
> nem RTC-képes lábra teszi a reset gombot), az eszköz a „csak gombbal" alvások
> esetén is armol egy 1 órás időzítőt – így nem válhat elérhetetlenné.

> ⚠️ A relé a `D10` = **GPIO10** lábon van, **22 kΩ lehúzó ellenállással a GND
> felé** (a korábbi `D5` = GPIO7 bekötés nem volt stabil; a `D10` nem strapping
> láb). Deep sleep alatt az ESP32-C3 `GPIO6–21` lábai – köztük a GPIO10 –
> alapból nagyimpedanciásak. A firmware ezért minden elalvás előtt **rögzíti a
> relé lábát LOW-ra** (`gpio_hold_en` + `gpio_deep_sleep_hold_en`), és ébredés
> után – miután a lábat már maga hajtja – oldja fel. A 22 kΩ lehúzó ettől még
> **nem elhagyható**: a bekapcsolás és a program indulása közti ablakban a hold
> még nem él.

---

## 11. Állapotjelzés a LED-eken

| Státusz LED (`D4`) | Wi-Fi LED (`D3`) | Jelentés |
|---|---|---|
| be | be | Minden rendben, van Wi-Fi |
| be | ki | Nincs Wi-Fi kapcsolat (pl. a router bootolására várunk) |
| ki | ki | Alvás |
| be | **villog 1 Hz** | **AP beállító mód** – várja a böngészőt (`AP_BLINK_MS`) |
| **villog 2 Hz** | ki | **Router reset pulzus** – a router épp áram nélkül (`RESET_BLINK_MS`) |
| **együtt villog 5 Hz** | **együtt villog 5 Hz** | **Végzetes hiba** (LittleFS / konfig / watchdog) |
| **felváltva villog 5 Hz** | **felváltva villog 5 Hz** | **Beragadt gomb** induláskor |

A négy villogó jelzés szándékosan elkülönül: az **AP mód** és a **router reset**
csak az egyik LED-et villogtatja (a másik állapota is más), a két hibajelzés
pedig mindkettőt, gyorsabban – az egyik együtt, a másik ellenfázisban.

---

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

Minden bejegyzés uptime bélyeget, egy eseménykódot és egy paramétert tartalmaz:

| Esemény | A paraméter jelentése |
|---|---|
| `BOOT` | az indulás oka (`esp_reset_reason()`) |
| `WIFI OK` | hányadik újrapróbálkozási körben sikerült |
| `WIFI LOST` | `WiFi.status()` a kiesés pillanatában |
| `TEST FAIL` | a teszt sorszáma **1-alapon**, 1–5 (csak a hibasorozat **első** tagja) |
| `ROUTER RESET` | hányadik reset esemény (1–4) |
| `AP MODE` | 1 = nincs mentett SSID, 2 = hitelesítési hiba, 3 = letelt a 2 nap, 4 = a gateway sem érhető el |
| `GW UNREACH` | 1 = a router reset előtt, 2 = a reset után is |
| `CONFIG SAVED` | 0 |
| `SLEEP` | 1 = újrapróbálkozás, 2 = tartós internetkiesés, 3 = AP időtúllépés, 4 = végzetes hiba |
| `FATAL` | 1 = LittleFS csatolás, 2 = konfiguráció olvasás, 3 = watchdog, 4 = a wifireset törlése nem sikerült |
| `WDT RESET` | hányadik rendellenes újraindulás |
| `STUCK BUTTON` | 0 = reset gomb, 1 = wifireset gomb (csak az **első** kör – az ismétlődő 60 mp-es alvás-ébredés körök nem íródnak be újra) |

A `/log` oldal az aktuális állapotot is mutatja: reset ok, watchdog számláló,
újrapróbálkozási körök, uptime.
