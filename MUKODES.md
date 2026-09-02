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
| Kimenetek alapállapotba (relé `LOW`, státusz LED be, Wi-Fi LED ki) | azonnal | |
| Soros port indítása | max. **3 mp** + 0,5 mp | |
| **Watchdog bekapcsolása** | azonnal | Innentől a `setup()` maradéka is felügyelt – lásd 8. pont |
| Beragadt gomb ellenőrzés (**mindkettő**: `D0` és `D1`) | azonnal | Ha bármelyik nyomva: **3 mp türelem az elengedésre**, utána LED-ek **felváltva** villognak 3 mp-ig, majd **60 mp** deep sleep |
| Watchdog reset számláló ellenőrzés | azonnal | 3. rendellenes reset → `MODE_FATAL` |
| LittleFS csatolás (első indításkor **formázás**: 4–7 mp) | azonnal | Hiba → `MODE_FATAL` |
| Konfiguráció beolvasása | azonnal | Olvasási hiba → `MODE_FATAL` |
| Wi-Fi csatlakozás | max. **20 mp** | siker → `MODE_MONITOR` |

> A `MODE_FATAL` **nem állítja meg** a `setup()`-ot: az `enterFatal()` csak
> beállítja a módot és kiírja az okot, a `setup()` pedig végigfut (a Wi-Fi
> indítását már kihagyva), a jelzést utána a `loop()` végzi.

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

A várakozás akkor zárul korábban, ha **mindkét lépés** sikerül. A próba
**ismétlődik**, tehát elég, ha a kapcsolat a várakozás *bármely* pontján
helyreáll – a kilépés az azt követő ütemben megtörténik. A teljes időt csak
akkor várjuk ki, ha végig nincs meg (mérve: `OP7`).

Egy kör két próbálkozási ablakot ad (a router reset előtt és után), így az első
kör önmagában ~25 percet fed le. Ami ebből kimarad, azt a következő körök
kapják el. A tényleges határ **46,0 óra** (mérve: `R8`).

---

### Áramszünet

Áramszünetkor az **ESP és a router együtt áll le**, és visszatéréskor együtt is
indul – csakhogy az ESP másodpercek alatt bootol, a router percek alatt. Ha az
ESP a saját mércéje szerint azonnal tesztelne, „nincs internet"-et látna, és
percekkel az áramszünet után elsőként azt tenné, amit épp nem szabad: elvenné a
routertől az áramot, pont bootolás közben.

Két dolog véd ez ellen:

1. a **10 perces türelmi idő** (`firstStartDelay`) **minden** hidegindulásnál,
2. és a **60 másodpercenkénti próba**, ami a türelmi időt lezárja, amint a
   router ténylegesen felállt – tehát a 10 perc nem elvesztegetett idő.

| helyzet | mért eredmény |
|---|---|
| a router 3 perc alatt feláll | az ESP 3 perc 21 mp-kor kezd tesztelni, a reléhez **hozzá sem nyúl** (`PWR1`) |
| a router egyáltalán nem áll fel | az első router-újraindítás **12 perckor** (`PWR2`) |

Ami az áramszünettel **elvész**: a diagnosztikai napló és minden RTC-számláló
(a watchdog-számláló tiszta lappal indul). Ez szándékos – az áramtalanítás
emberi beavatkozás, tehát tiszta lap. Ami **megmarad**: a LittleFS-en tárolt
Wi-Fi konfiguráció.

> A relé a bekapcsolás és a program indulása közti ablakban is `LOW` marad, mert
> a **22 kΩ lehúzó ellenállás** tartja – szoftver ilyenkor még nem fut. Enélkül
> egy áramszünet után a router nem is kapna áramot vissza.

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

### Milyen sűrűn kapcsolja ki a routert?

A `maxfailureEvents = 5` felső korlátja **4 tényleges újraindítás**, utána
1 órás alvás – ezen a korai kilépés nem változtat, csak a *tempón*. Mérve:

| Helyzet | Két újraindítás között | Újraindítások |
|---|---|---|
| **Befagyott router-DNS** (a ping megy, a HTTP nem) | **4 perc 34 mp** | 4 (`RR1`) |
| **Teljes internetkiesés** (a ping sem megy) | **13 perc 38 mp** | 4 (`RR2`) |

A különbség szándékos, és épp a helyes irányba mutat. A korai kilépés csak
akkor rövidít, ha az `onlineProbe()` pingje **sikerül** – vagyis az IP-szintű
út él, és mégsem megy a HTTP. Ez pontosan a beragadt router esete, ahol a
gyorsabb újraindítás indokolt. Ha az internet tényleg halott, a ping is bukik,
korai kilépés nincs, és a routernek szánt **10 perces bootvárakozás
érintetlen** – az új mechanizmus tehát nem teszi agresszívebbé a firmware-t
ott, ahol az ártana.

Egyetlen sikeres teszt **nullázza a reset-számlálót** (`RR3`), tehát az eszköz
nem „gyűjtögeti" a resetet napokon át.

---

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
| Wi-Fi LED **ki** (a kapcsolat tényleg nincs meg), státusz LED marad **be** | azonnal |
| **3 újracsatlakozási próba**, köztük 30 mp | max. ~2 perc |
| Sikertelen → azonnal **router újraindítás** (4. pont szerint) | |

Nem várunk további teszt ciklusokat: ha nincs Wi-Fi, a tesztnek nincs értelme.

### Ha te magad áramtalanítod a routert

**Az eszköz ezt nem tudja megkülönböztetni attól, hogy a router lefagyott** – a
hálózat mindkét esetben ugyanúgy eltűnik. Ezért ugyanaz történik, mint egy
valódi hibánál: kivárja a 3 újracsatlakozási próbát, és ha addig sem jön vissza
a hálózat, ő is elveszi a router áramát.

| | mért érték |
|---|---|
| **Türelmi idő** a hálózat eltűnésétől az ESP saját router-resetjéig | **2,0 perc** (`PWR3`) |
| Ha a router ezen belül visszajön | az ESP **hozzá sem nyúl** a reléhez, csendben visszacsatlakozik (`PWR4`) |

Gyakorlatban: **ha kézzel dugod vissza a routert 2 percen belül, nem történik
semmi.** Ha tovább tart neki felállni, az ESP is ad neki egy 90 másodperces
áramtalanítást, majd 10 percig vár – kellemetlen, de magától rendbe jön, és a
routernek sem árt.

> Ha ez a 2 perc kevésnek bizonyul a te routerednél, a `wifi_maxRetries` (3) és
> a `wifiInterval` (30 mp) növelésével hosszabbítható. Az ára, hogy egy valódi
> router-fagyást is ennyivel később kezel le.

---

## 6. AP beállító mód

| Esemény | Időtartam |
|---|---|
| Portál elérhető: `192.168.4.1` | |
| Jelzés: **Wi-Fi LED villog 1 Hz**, státusz LED végig **be** | azonnal |
| Az űrlap előkitöltése | SSID, IP, gateway a mentett értékkel (HTML-escape-elve); **a jelszó soha** |
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
- **Fájlírás közben az eszköz soha nem alszik el** (a leállási út a zárat meg
  is szerzi, nem csak megvárja az írást), tehát nem maradhat félig
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
| A számláló nullázódik | áramtalanításkor, vagy **1 óra** hibátlan működés után – **minden üzemmódban**, az AP portált és a first start várakozást is beleértve (mérve: `WDT9`) |
| Az „1 óra" mihez képest | a **mostani indulás** kezdetéhez (`timing.startMillis`), nem abszolút `millis()` értékhez (`WDT14`, `WDT15`) |

Az utolsó sor apróságnak tűnik, de ez volt a program **egyetlen abszolút
`millis()` összehasonlítása** – a másik 23 időzítés mind különbség-alakú. Két
hallgatólagos feltevést hordozott: hogy a `millis()` minden induláskor nulláról
kezd (ez igaz, lásd a 15. fejezet uptime-megjegyzését), és hogy nem fordul körbe
(ez 49,7 napig igaz). A különbség-alak mindkettőtől függetlenné teszi, és a
`timing.startMillis` maga mondja ki, mihez mérünk.

> Ezért a `timing.startMillis` mezőt a `firstStart` lezárása után **sem
> frissítjük**: végig a boot időbélyege marad. Egy frissítés eltolná a
> watchdog-ablakot.

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

### Pattogás (kontaktus-visszaugrás)

Egy mechanikus nyomógomb sem a lenyomáskor, sem a **felengedéskor** nem ad
tiszta élt: a kontaktus néhány tized- vagy ezredmásodpercig ide-oda ugrál, és
ez alatt több megszakítás is keletkezik. A kezelés három ponton pattogástűrő:

| | hogyan |
|---|---|
| **Pollozott ág** | minden `HIGH` olvasás nullázza a lenyomás-időt, tehát a debounce elölről indul – csak a *ténylegesen* végig lenyomott 50 ms számít |
| **Megszakításos retesz** | a lefutó él újraírja a lenyomás idejét, a felfutó pedig **nullázza is** – így a felengedési pattogás után nem marad „félig lenyomott" állapot (`BNC1`) |
| **Beragadt-gomb ellenőrzés** | a döntést **egyetlen** beolvasás hozza, nem kettő – különben a két olvasás közé eshetne egy felengedési pattanás, és a már elengedett gombot beragadtnak minősítené (`BNC4`) |

Miért fontos, hogy a felfutó él **nullázza** a lenyomás idejét? Ha nem tenné, a
felengedési pattogás után egy elavult „lenyomva vagyok"-időbélyeg maradna hátra.
Egy jóval későbbi, magában ártalmatlan felfutó él azt látná, hogy a gomb
„régóta" nyomva van, és **azonnal reteszelne** – vagyis az eszköz magától
újraindulna. Ezt a `BNC1` kifejezetten méri.

Amit a rendszer **szándékosan nem** fogad el: egy folyamatosan recsegő, kopott
gomb (10 ms-onként váltakozó kontaktus) **nem** indít újra és nem is reteszel
(`BNC3`). Egy elhasznált gomb miatt az eszköz ne induljon újra magától – inkább
ne reagáljon, mint hogy spontán újrainduljon.

> ⚠️ A Wi-Fi reset gomb lába (`D0` = **GPIO2**) az ESP32-C3 egyik *strapping*
> lába: a chip csak akkor bootol, ha a reset pillanatában GPIO2 = 1.
> **Fájlírás közben egyik gomb sem hat.** Mindkét gombkezelő **atomikusan**
> megszerzi a konfigurációs zárat (`beginConfigWrite()`), mielőtt újraindítana
> vagy törölne – a puszta jelző-ellenőrzés kevés lenne, mert a gyors kapu és a
> tényleges újraindítás között az aszinkron webszerver taskja még elindíthatna
> egy mentést. Ha a zár nem szerezhető meg, a következő 10 ms-os mintavételi
> kör újra próbálkozik. (Mérve: `BTN4`.)

> **Mennyire gyorsan reagál?** A firmware minden saját várakozó ciklusában
> **10 ms-onként** olvassa mindkét gombot – a `waitWithButtons()`, a
> `waitWithButtonsUntilOnline()`, az `initWiFi()` 20 mp-es várakozása, a reset
> pulzus, a `handleFirstStart()` és a `loop()` minden ága beleértve (mérve:
> `BTN1`, a leghosszabb ablak 10 ms).
>
> Egyetlen kivétel a futó **HTTP teszt**: a `http.GET()` a core blokkoló
> hívása, nincs benne visszahívás, tehát alatta egyik gombot sem *pollozzuk*.
> Ez az ablak legfeljebb **egy kérésnyi** – 15 mp hallgató szervernél, 33 mp
> halott DNS-nél (mérve: `BTN2`).
>
> Ezt **megszakítás-alapú retesz** hidalja át (`btnResetLatched`,
> `btnWifiResetLatched`). Mindkét lábon `CHANGE` megszakítás fut, és a kezelő
> **megméri a lenyomás hosszát**: lefutó élen jegyzi az időt, felfutón csak
> akkor reteszel, ha a gomb legalább `BUTTON_DEBOUNCE_MS`-ig lent volt. A
> debounce tehát változatlan, csak hardveresen történik. A blokkoló szakasz
> alatti gombnyomás nem vész el, csak késik (`LAT1`).
>
> Amit a retesz **nem** kerül meg: a zajtüske nem reteszel (`LAT2`), a
> `savingConfig` kaput és a konfigzárat a feldolgozás tiszteletben tartja, és
> foglalt zárnál a jelző **megmarad**, hogy a nyomás ne vesszen el (`LAT4`,
> `LAT6`). Beragadt gomb nem reteszel, mert felfutó éle sosem lesz (`LAT5`).
> Alvás előtt a megszakításokat leválasztjuk, még az ébresztőforrás élesítése
> előtt (`LAT7`).

> **Bekapcsolás közben ne tartsd nyomva** – az eszköz el sem indulna, és ez
> ellen szoftver nem védhet. Új hardver revízióban a gombot érdemes szabad,
> nem-strapping lábra tenni (pl. `D2` = GPIO4).

### A mezők validálása – közös szabály

Mind a négy mező **ugyanúgy** viselkedik: előbb levágjuk a másolás-beillesztés
szóközeit, és **csak azután** mérjük a hosszt. Enélkül egy határértékes érték
egyetlen beillesztett záró szóközzel „túl hosszú" hibát adna – holott a vágott
érték tökéletesen érvényes, és a beolvasás úgyis vágna. A túlméretes érték
viszont továbbra is hiba: a vágás nem menti meg. (Mérve: `AP7`.)

> A soros portra **csak a négy ismert mezőt** visszhangozzuk, és a hosszt is
> korlátozzuk (`%.64s`). Az ismeretlen paramétereket amúgy sem dolgozzuk fel,
> tehát diagnosztikai értékük nincs – egy sok mezős POST viszont annyi sort írt
> volna, ahány mezőt küldtek, mindezt az `async_tcp` taskban, ami közben a
> webszervert is kiszolgálja.

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

Induláskor **mindkét** gombot ellenőrizzük. Ha bármelyik nyomva van, előbb
**3 másodpercet várunk arra, hogy elengedjék** – és csak ha addig sem engedték
el, akkor minősül beragadásnak:

1. A két LED **felváltva** villog **3 másodpercig** (ellentétes fázisban)
2. Az eszköz **60 mp** deep sleepbe megy
3. **Gombos ébresztés nélkül** – különben a beragadt gomb azonnal
   újraébresztené, és végtelen boot loop lenne
4. Az esemény bekerül a diagnosztikai naplóba (`STUCK BUTTON`, a paraméter
   megmondja melyik gomb)

> **Miért kell a 3 másodperces türelem?** A reset gomb **LOW szintre** ébreszt
> az alvásból, tehát a felhasználó szükségképpen **még nyomva tartja**, amikor
> az eszköz bootolni kezd – minden gombos ébredés ilyen. Egy pillanatnyi
> beolvasás alapján egy teljesen normális, fél-egy másodperces gombnyomás is
> „beragadásnak" minősülne, és az eszköz azonnal visszaaludna 60 mp-re: a
> felhasználó szemszögéből „a gomb nem csinál semmit", pedig épp ő nyomta meg.
>
> Régen a döntést a `Serial.begin()` utáni várakozás hossza szabta meg, ami
> viszont attól függött, **van-e USB gazda**: bedugott kábellel a határ ~600 ms
> volt, kábel nélkül ~3,5 mp. Ugyanaz a gombnyomás máshogy sült el aszerint,
> hogy be volt-e dugva a kábel. A `STUCK_BUTTON_CONFIRM_MS` ezt a küszöböt
> kimondottá és a kábeltől függetlenné teszi. (Mérve: `SH3`.)

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

Az alvás és az újraindulás **közös torlópontja**, hogy fájlírás közben egyik
sem történhet meg. A várakozás nem csak megvárja a folyamatban lévő mentést,
hanem **meg is szerzi a konfigurációs zárat**: enélkül a várakozás vége és a
tényleges `esp_deep_sleep_start()` / `ESP.restart()` közötti néhány
ezredmásodpercben (két-három `println` és egy `Serial.flush()`) az AsyncTCP
task még elindíthatna egy új mentést, amit aztán a leállás félbevágna – épp az,
ami ellen a várakozás van. Ugyanez a hiba szerepelt korábban a gombos
újraindításban is, és ott is a zár **atomikus** megszerzése oldotta meg.
(Mérve: `SH1`.) A határidős kilépés megmarad: ha a jelző bármiért beragadna,
5 mp után az eszköz a zár nélkül is továbblép, tehát nem fagyhat le tőle.

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

> **A Wi-Fi LED késhet, legfeljebb egy `SUCCESS_DELAY`-t.** A kapcsolat
> állapotát a `TESTING_STATE` ellenőrzi; a sikeres teszt utáni 60 mp-es
> várakozás alatt nincs `WiFi.status()` lekérdezés. Ha a kapcsolat épp ekkor
> szakad meg, a LED **legfeljebb 60 másodpercig** még kapcsolatot mutat, aztán
> a következő teszt elején elalszik (mérve: `LED4` – pontosan 60 mp). A
> viselkedésre ez nem hat: a hibát a következő teszt így is elkapja.

---

## 12. Heap felügyelet

A sketch maga **semmit nem allokál dinamikusan** – az ESPAsyncWebServer, az
AsyncTCP és a Wi-Fi driver viszont igen. Egy lassú szivárgás vagy elaprózódás
hónapokig észrevétlen marad, aztán egy nap az eszköz „csak úgy" nem működik: egy
allokációs hiba esetén a legtöbb könyvtár **csendben elbukik**, nem panikol,
tehát a watchdog sem fogja meg. Ezért mérünk, kiírunk, és végső esetben magunktól
újraindulunk – **mielőtt** bármi elromlana.

| Paraméter | Érték |
|---|---|
| Mintavétel | **10 mp**-enként (`HEAP_CHECK_INTERVAL_MS`) |
| Rendszeres állapotsor | **30 perc**enként – 0,03 sor/perc, a 30 sor/perces költségvetésből semmit nem visz el |
| Figyelmeztetés | a szabad heap **25 000 B** alatt (`HEAP_WARN_BYTES`) |
| Önkéntes újraindulás | a szabad heap **12 000 B** alatt, **vagy** a legnagyobb összefüggő tömb **6 000 B** alatt |
| …de csak ha kitart | **3 egymást követő** mérésen át (30 mp) |
| Legfeljebb | **3** heap-újraindulás, utána `MODE_FATAL` |

A soros porton így néz ki:

```
Uptime: 0d 0h 0m 10s
Heap: szabad 178432 B, legnagyobb tomb 110000 B, valaha volt legkisebb 178432 B
```

A harmadik érték a legfontosabb: **a valaha volt legkisebb** szabad heap. Egy
lassú szivárgás ebből látszik meg akkor is, ha a pillanatnyi érték épp rendben
van.

> A figyelmeztetés csak a küszöb **átlépésekor** szól, nem minden mérésnél –
> ugyanaz a „csak a sorozat első tagja" szabály, mint a `TEST FAIL` /
> `WIFI LOST` esetében. Visszaállni is csak akkor tud, ha a heap érdemben (10%)
> a küszöb fölé ment, hogy a küszöb körül ingadozva ne kapcsolgasson.
> (Mérve: `HP2`.)

### Miért két küszöb?

Lehet 30 KB szabad heap úgy, hogy a legnagyobb **összefüggő** tömb csak 4 KB –
ilyenkor a szabad heap önmagában megnyugtató, közben a foglalások már buknak.
Ezért a legnagyobb tömböt (`ESP.getMaxAllocHeap()`) külön is nézzük.

### Mikor NEM indul újra

Négy kizárás van, és mindegyik arról szól, hogy az újraindulás ne rontson
többet, mint amennyit javít (mérve: `HP5`):

| Helyzet | Miért | Van-e kiút? |
|---|---|---|
| **AP beállító mód** | a felhasználó épp a portálon dolgozik, az újraindulás eldobná a beírt adatokat | igen: a portál 5 perc tétlenség után elalszik, abból friss bootolás jön |
| **`MODE_FATAL`** | a program már nem futtat állapotgépet | igen: 5 perc után deep sleep |
| **Fájlírás közben** | félbevágott mentés | a következő mérés újra próbálja |
| **A relé impulzusa alatt** | a router épp áram nélkül van, az újraindulás félbevágná a 90 mp-es pulzust | a pulzus végén újra próbálja |
| **A router reset utáni ellenőrző ablakban** | lásd lent – ez a legkevésbé nyilvánvaló kizárás | az ablak (max. ~13 perc) végén azonnal |

#### A router reset utáni ellenőrző ablak

A gateway-eszkaláció **kétfázisú**, és a két fázis között akár 10 perc is
eltelhet (`RESET_DELAY`):

1. A saját gateway sem érhető el → `GW UNREACH(1)` → **a router kap egy esélyt**:
   újraindítás, majd várakozás.
2. A várakozás után **újra** ellenőrizzük. Ha a gateway még mindig nem válaszol,
   a statikus IP a rossz – a routert nincs értelme tovább áramtalanítani, jön az
   **AP beállító mód**, hogy javítani lehessen.

Azt, hogy „az első fázis már lefutott", **három sima globális hordozza együtt**:
`currentState`, `uiFlags.resetPrinted` és `timing.stateStart`. Egy újraindulás
mindhármat elveszti – és a `timing.stateStart`-ot **átvinni sem lehetne**
értelmesen, mert a `millis()` ébredéskor nulláról indul.

Az eszköz így elölről kezdené: megint az első fázis futna le, vagyis a router
kapna **még egy fölösleges áramtalanítást**, és a második fázis döntése – az AP
módba menetel – csak egy teljes körrel később születne meg. Épp az járna
rosszul, akinek a statikus IP-jét javítani kellene.

Ezért itt inkább **várunk**. Az ablak korlátos (`RESET_DELAY` + a
visszacsatlakozás, ~13 perc), a kritikus-számláló pedig telítve marad, tehát az
ablak bezárultával az újraindulás azonnal megtörténik. (Mérve: `GWH1`, `GWH2`.)

### Mit visz át az újraindulás

Egy `ESP.restart()` a RAM-ot törli, tehát minden sima globális elveszik. A
többségükért nem kár: a `timing` újraszámolódik, a `firstStart` várakozás újra
lefut (és a próba perceken belül le is zárja), a `cycleIndex`/`failedCount`
pedig csak azt mondja meg, hol tart az öt teszt között.

**Egy kivétel van**, és annál az elvesztés viselkedési hibát okozna:

> A `testState.resetEvents` számolja, hányszor indítottuk már újra a routert
> ebben a sorozatban, és ez viszi az eszközt az ötödiknél az **1 órás alvásba**
> (`internetFailSleep`). Ha egy heap-újraindulás ezt nullázná, a számláló mindig
> elölről kezdene – vagyis az eszköz **végtelenül újraindíthatná a routert**
> ahelyett, hogy elalszik.

Ezért ez az egy érték átmegy az RTC memórián (`rtcCarryResetEvents`), és a
`setup()` **pontosan egyszer** használja fel: utána visszaállítja a „nincs
átvitel" jelölésre, hogy egy későbbi, más okból történt újraindulás (gomb,
watchdog) ne támasszon fel elavult értéket. (Mérve: `HP4`.)

Ami eleve túléli: a diagnosztikai napló és a watchdog-számláló (`RTC_NOINIT`),
valamint a LittleFS-en tárolt konfiguráció.

#### A 2 napos ablak – a másik többfázisú létra

Ha a hálózat egyáltalán nem látszik, a program **33 kört** próbál, köröként egy
órás alvással – kb. két napig. Utána AP beállító módba megy, hogy a felhasználó
javíthassa a konfigurációt. A köröket az `rtcRetryRounds` számolja.

Ez a számláló **szándékosan `RTC_DATA_ATTR`**, nem `RTC_NOINIT`: a deep sleepet
túléli, de bekapcsoláskor **és szoftveres resetre nullázódik** – „a felhasználói
beavatkozás tiszta 2 napos ablakkal indít". Gombnyomásra ez helyes.

**A heap miatti újraindulás viszont nem felhasználói beavatkozás** – a saját
döntésünk –, mégis ugyanolyan szoftveres reset. Átvitel nélkül tehát minden
heap-újraindulás **nullázta volna a 2 napos ablakot**: az eszköz sosem érte
volna el a határt, vagyis **sosem ment volna AP módba** – örökké
újrapróbálkozna, és a felhasználó sosem kapna esélyt a javításra.

Ezért ezt is átvisszük – de csak a *saját* újraindulásunkon. Gombnyomásnál nincs
átvitel, tehát a tiszta lap ott megmarad. (Mérve: `GWH4`, `GWH5`.)

#### A teljes lista: mi vész el, és miért nem baj

| Változó | Mi lesz vele | Miért rendben |
|---|---|---|
| `testState.resetEvents` | **átvisszük** | enélkül végtelenül újraindíthatná a routert |
| `rtcRetryRounds` (2 napos ablak) | **átvisszük** | lásd lent – enélkül sosem érné el az AP módot |
| `testState.cycleIndex`, `failedCount` | elvész | csak azt mondja meg, hol tart az öt teszt között – egy új tesztkör ~1 perc (`GWH3`) |
| `testState.resetStep` | elvész | **nem lehet** félbehagyott pulzus: a relé impulzusa alatt nincs újraindulás |
| `currentState`, `uiFlags.resetPrinted`, `timing.stateStart` | elvész | **ezért van az ellenőrző ablak kizárása** – lásd fent |
| `timing.startMillis` | újraszámolódik | a `setup()` beállítja |
| `timing.fatalStart` | elvész | `MODE_FATAL`-ban nincs újraindulás |
| `uiFlags.firstStart` | visszaáll `true`-ra | a 10 perces várakozás újra lefut, de a próba perceken belül lezárja |
| `staticConfigActive` | újraszámolódik | az `initWiFi()` állítja be |
| `deviceMode`, `savingConfig`, `restartPending` | elvész | mindhárom állapotban ki van zárva az újraindulás |
| `ssid`, `pass`, `ipStr`, `gatewayStr` | elvész | a LittleFS-ről újratöltődik |
| a gombok reteszei | elvész | a nyomás úgyis újraindítást kért, és az megtörténik |

> ⚠️ **A mintavétel a `loop()` iterációi között történik**, tehát a hosszú
> blokkoló várakozások alatt (`RESET_DELAY`, a 90 mp-es relé pulzus, egy futó
> HTTP kérés) nincs mérés. Ott nem is allokál semmi érdemben, de a `/log` oldal
> uptime-bélyegein ez látszani fog: a heap sorok között ilyenkor nagyobb a rés.

### Új eseménykódok

| Esemény | A paraméter jelentése |
|---|---|
| `LOW HEAP` | a szabad heap **KB**-ban a küszöb átlépésekor |
| `HEAP RESTART` | a szabad heap **KB**-ban az önkéntes újraindulás előtt |
| `FATAL` **5** | tartósan kevés a heap – az újraindítás sem segített |

---

## 13. Két task, egy memória

A programban két task fut: a `loop` task és az AsyncTCP webszerver
**`async_tcp`** taskja (ez utóbbi csak AP beállító módban). Néhány globálishoz
mindkettő hozzáfér – itt van egy helyen, mi védi őket:

| Globális | `async_tcp` | `loop` | Mi védi |
|---|---|---|---|
| `apDeadline` | ír (mind a 4 kezelő) | olvas | `volatile`, igazított 32 bites szó – a C3-on egyetlen utasítás |
| `restartPending`, `restartAt` | ír (POST) | olvas | `volatile` |
| `savingConfig` | foglal/elenged (POST) | foglal (gombok, leállás) | **spinlock** (`beginConfigWrite()`) |
| `rtcEvents`, `rtcEvNext` | ír (`CONFIG SAVED`), olvas (`/log`) | ír, olvas | **`evLogMux`** kritikus szakasz |
| `rtcWdtResets`, `rtcRetryRounds` | **csak olvas** (`/log`) | ír | igazított szó – a lapon legfeljebb egy pillanattal régi érték áll |
| `ssid`, `pass`, `ipStr`, `gatewayStr` | ír (POST 2. fázisa) | olvas (`WiFi.begin()`) | **szerkezeti invariáns**, lásd lent |

### A négy konfigurációs puffer

Ezt a négyet **nem zár védi**, hanem az, hogy a két hozzáférés sosem eshet
egybe:

> Az `initWiFi()` és az `onlineProbe()` csak olyan helyekről fut, ahol a
> beállító portál nem létezik.

Miért áll ez?

- A portált egyedül a `startConfigPortal()` indítja, az pedig `MODE_CONFIG`-ot
  állít, és a `server.begin()` az **utolsó sora**.
- A `loop()` a `MODE_CONFIG` és a `MODE_FATAL` ágának elején **visszatér** –
  egyik ág sem ér el sem `initWiFi()`-ig, sem `onlineProbe()`-ig.
- **Visszaút nincs:** `MODE_CONFIG`-ból csak újraindulással vagy `MODE_FATAL`-ba
  lehet kilépni; `MODE_MONITOR`-ba egyik sem vezet vissza. A `setup()` két
  `MODE_MONITOR` értékadása a portál létrejötte **előtt** van.
- Két kezelő **egymással sem** versenyez: az ESPAsyncWebServer minden kezelőt
  ugyanazon az `async_tcp` taskon, sorosan hív – tehát a GET űrlap (ami olvassa
  az SSID-t) és a POST (ami írja) sem futhat egyszerre.

Mérve: `CC1` (a portál futása alatt a `loop` egyetlen `WiFi.begin()`-t sem ad
ki, miközben mentések érkeznek), `CC2` (ugyanez a `MODE_FATAL` ágon, ahol a
webszerver **még fut**, mert a `server.end()` csak az `enterDeepSleep()`-ben
van), `CC3` (két mentés között a négy puffer mindig együtt vált át).

> **Ha ez valaha megváltozna** – bármi, ami a portál futása közben hívna
> `initWiFi()`-t vagy `onlineProbe()`-ot –, a négy puffert zár alá kell tenni.

---

## 14. A soros port

| | |
|---|---|
| Sebesség | **115200** baud |
| Indulás | `Serial.begin()`, majd max. **3 mp** várakozás a gazdára + **0,5 mp** CDC-beállás – csak ezután megy ki az első sor, hogy egy frissen csatlakoztatott monitoron ne vesszenek el az induló üzenetek |
| Lezárás alvás előtt | `Serial.flush()`, majd `Serial.end()` – **a legvégén**, minden más leállítási lépés után |
| Lezárás újraindítás előtt | `Serial.flush()` közvetlenül az `ESP.restart()` előtt |

**A lezárás után egyetlen sort sem írunk** – az elveszne a deep sleepben.
Mérve: `SER6` (a normál alvási út) és `SER7` (a beragadt gomb ága).

> A beragadt-gomb ág volt az egyetlen kivétel: ott a `flush()` a 3 mp-es
> villogás **előtt** állt, a relé lábát rögzítő `holdRelayForSleep()` viszont a
> villogás **után** fut – és az ki tud írni egy figyelmeztetést, ha a rögzítés
> nem sikerül. Épp az a sor veszett volna el, amiért az ember a soros portot
> nézi. Most ott is a legvégén zárunk le.

### Mennyit ír?

A soros kimenet **nem áraszthatja el** a konzolt – se normál működésben, se
tartós hibában. A költségvetés **30 sor/perc**, és ezt nem csak a szokásos
esetekben mértük meg, hanem a legellenségesebbekben is:

| Helyzet | Mért |
|---|---|
| Normál működés | 9 sor/perc (`SER1`) |
| Tartós internetkiesés | 9 sor/perc (`SER2`) |
| AP mód, végzetes hiba (villogó ciklus) | **0** a ritkított heap-soron kívül (`SER3`) |
| **Pislákoló Wi-Fi** (0,5–5 mp) | 9–12 sor/perc (`LOG4`) |
| **A küszöb körül ingadozó heap** | 15 sor/perc (`SER8`) |
| **Mindig bukó naplómentés** tartós kiesés alatt | 10 sor/perc (`SER9`) |
| A 33 körös, kétnapos létra | `SER10` |

> A pislákoló Wi-Fi korábban **132 sor/perc**-et adott: az óraszinkron minden
> újracsatlakozásnál bejelentkezett. Ezért jelent be a `startNtp()` csak
> egyszer bootonként.

Ezt három szabály tartja fenn:

- **Az ismétlődő események csak egyszer.** A `TEST FAIL`, a `WIFI LOST` és a
  `STUCK BUTTON` sorozatokból csak az első kerül ki (lásd a 15. fejezetet).
- **A villogó ciklusok némák.** A LED-kezelés nem ír semmit.
- **Sikeres teszt = egy sor.** A `testInternetHTTP()` sikernél nem írja ki sem
  a kapott törzset, sem külön visszaigazolást – csak eltérésnél, ahol a kapott
  törzs a diagnózishoz kell.

---

## 15. Diagnosztikai napló

Az eszköz az utolsó **32 eseményt** RTC memóriában tárolja, és a beállító
portál `/log` oldalán kiírja. Soros kábel nélkül is megtudható, mi történt.

| Túléli? | |
|---|:---:|
| Deep sleep | igen |
| Watchdog / panic reset | igen |
| Reset gomb | igen |
| **Áramtalanítás** | **nem** – de a fájlba mentett napló igen, lásd lent |

`RTC_NOINIT_ATTR`-ben van, ezért éli túl a resetet is – pont azokat a hibákat,
amiket ki akarunk vizsgálni. Mérete **392 bájt** (2 × 4 bájt fejléc + 32 × 12
bájt bejegyzés) a C3 ~8 KB-os RTC memóriájából; a program összes RTC állapota
együtt is ~420 bájt.

Minden bejegyzés uptime bélyeget, egy eseménykódot és egy paramétert tartalmaz:

> ⚠️ **Az uptime bélyeg az AKTUÁLIS indulás óta telt időt jelenti, nem az
> összeset.** Deep sleepből ébredve az `esp_timer` (és így az Arduino
> `millis()` is) **nulláról indul újra** – az ESP-IDF ezt kimondottan leírja: a
> számláló csak *light* sleep után lép előre az alvás idejével, és egyedül a
> `gettimeofday()` az, ami a deep sleepet is átvinné. A naplóban tehát az
> uptime minden `BOOT` sornál visszaáll nullára; a `BOOT` bejegyzések jelölik
> az alvás-határokat.

| Esemény | A paraméter jelentése |
|---|---|
| `BOOT` | az indulás oka (`esp_reset_reason()`) |
| `WIFI OK` | hányadik újrapróbálkozási körben sikerült |
| `WIFI LOST` | `WiFi.status()` a kiesés pillanatában (csak a kiesés-sorozat **első** tagja) |
| `TEST FAIL` | a teszt sorszáma **1-alapon**, 1–5 (csak a hibasorozat **első** tagja) |
| `ROUTER RESET` | hányadik reset esemény (1–4) |
| `AP MODE` | 1 = nincs mentett SSID, 2 = hitelesítési hiba, 3 = letelt a 2 nap, 4 = a gateway sem érhető el |
| `GW UNREACH` | 1 = a router reset előtt, 2 = a reset után is |
| `CONFIG SAVED` | 0 |
| `SLEEP` | 1 = újrapróbálkozás, 2 = tartós internetkiesés, 3 = AP időtúllépés, 4 = végzetes hiba |
| `FATAL` | 1 = LittleFS csatolás, 2 = konfiguráció olvasás, 3 = watchdog, 4 = a wifireset törlése nem sikerült, 5 = tartósan kevés a heap |
| `WDT RESET` | hányadik rendellenes újraindulás |
| `STUCK BUTTON` | 0 = reset gomb, 1 = wifireset gomb (csak az **első** kör – az ismétlődő 60 mp-es alvás-ébredés körök nem íródnak be újra) |
| `LOW HEAP` | a szabad heap **KB**-ban a küszöb átlépésekor (csak az átlépés, lásd a 12. fejezetet) |
| `HEAP RESTART` | a szabad heap **KB**-ban az önkéntes újraindulás előtt |

A `/log` oldal az aktuális állapotot is mutatja: **az indulás oka szövegesen**
(a nyers enum-szám zárójelben marad, hibajelentéshez), watchdog számláló,
újrapróbálkozási körök, és az **uptime nap/óra/perc/mp alakban** – ugyanúgy,
ahogy a soros porton. A `Param` oszlop jelentését a lap alján lévő
jelmagyarázat írja le, tehát a naplóhoz nem kell forráskód.

A naplóoldalon **semmilyen konfigurációs érték nem jelenik meg** – sem az SSID,
sem az IP, sem a jelszó (nyíltan vagy kódolva). A mentés *ténye* viszont igen
(`CONFIG SAVED`), mert az a diagnózishoz kell. (Mérve: `LOG2`.)

### A napló mentése a fájlrendszerre

Az RTC napló az **áramszünetet nem éli túl** – épp azt a hibát nem, ami után a
leginkább tudni akarnánk, mi történt előtte. Ezért a program a **fontos
pillanatokban** kiírja a naplót a LittleFS-re is (`/evlog.bin`):

| Mikor | Miért |
|---|---|
| **Router reset előtt** | épp azért nyúlunk a hálózathoz, mert valami nem stimmel – itt a legnagyobb az esély egy áramszünetre |
| **AP módba váltás előtt** | az AP mód azt jelenti, hogy az eszköz feladta; épp ezt az előzményt akarja látni az, aki odamegy és megnyitja a portált |
| **Minden alvás előtt** | hosszú idő következik, ami alatt egy áramszünet elviheti az RTC naplót |

Az alvások mentése **egyetlen közös ponton**, az `enterDeepSleep()` elején
történik – így mind a négy alvási út (`retrySleep`, `internetFailSleep`,
`apSleep`, `fatalSleep`) megkapja, és egy **új** alvási úttól sem lehet
elfelejteni. Korábban a mentés külön-külön állt a két időzített alvásnál, és a
lejárt AP mód meg a végzetes hiba kimaradt belőle – pedig a végzetes hibánál a
legfontosabb, hiszen épp azt akarjuk később kivizsgálni. (Mérve: `NV9`.)
Csatolatlan fájlrendszernél a mentés magától kimarad, és nem akasztja meg az
alvást.

A fájl a teljes körpuffer pillanatképe, **kiegyenesítve** (a legrégebbitől a
legújabbig), így az olvasónak nem kell tudnia, hol tartott a gyűrű.

**Írás közben nincs alvás, újraindulás vagy másik írás.** Ezt nem külön kód
biztosítja, hanem ugyanaz a **konfigurációs zár**, amit a webes mentés és a
wifireset gomb használ – a mentés **atomikusan** szerzi meg. Ha a zár épp
foglalt, a mentés **kimarad**: nem várunk rá, mert a napló diagnosztika, és nem
szabad miatta blokkolni egy fontos műveletet. A következő fontos pillanatban
úgyis próbáljuk. A zárat a végén **feloldjuk** – eltérően a leállási úttól, ami
már nem tér vissza. (Mérve: `NV3`, `NV4`.)

**Az írás sikerességét visszaolvasással ellenőrizzük**, ugyanúgy, mint a
konfigurációs fájloknál: a siker nem az, hogy az írás nem panaszkodott, hanem
hogy a fejléc tényleg ott van. A **néma írási hiba** (a fájlrendszer sikert
jelent, de a tartalom nem kerül ki) csak így derül ki. **A sikertelenség nem
végzetes**: az RTC naplóban minden ott marad, a program dolga fontosabb – csak
szólunk róla. (Mérve: `NV2`.)

> A flasht nem koptatjuk feleslegesen: ha a legutóbbi mentés óta nem történt
> esemény, nem írunk újra. A `wifireset` gomb a négy konfigurációs fájlt törli,
> a naplófájlt **szándékosan nem**: nem tartalmaz konfigurációs értéket, és épp
> a törlés utáni diagnózishoz kell a leginkább. (Mérve: `LOG9`.)

> ⚠️ **A magic egyben verziójelző.** Az RTC `NOINIT` terület a szoftveres
> resetet – tehát egy **soros porton keresztüli firmware frissítést** is –
> túléli: az új firmware a régi tartalmat találja ott. Amikor az `EventEntry`
> 8-ról 12 bájtra nőtt (epoch mező), a régi bejegyzések új elrendezésként
> értelmezve szemetet adtak volna – és a mentés ezt a szemetet ki is írta volna
> a fájlrendszerre. Ezért: **ha az `EventEntry` vagy az `EVLOG_SIZE` változik, az
> `EVLOG_MAGIC`-et is emelni kell.** Az ára egyetlen bootnyi napló, cserébe
> nincs hamis diagnosztika. (Mérve: `LOG8`.)

### Melyiket tölti be az AP mód weboldala?

A fájl mindig az RTC napló egy **korábbi** pillanatképe. Ebből következik a
szabály:

| Helyzet | Melyik nyer | Miért |
|---|---|---|
| Mindkettőnek van **NTP időbélyege** | a **nagyobb** | ez a legpontosabb válasz, ezért ez az első szabály |
| Az RTC napló túlélte a mentés óta eltelt időt | **RTC** | bővebb is nála: mindent tartalmaz, ami a fájlban van, plusz ami azóta történt |
| Áramszünet törölte az RTC naplót | **fájl** | a fájl őrizte meg az előzményt – **ez a mentés értelme** |
| Az RTC-ben már 32 új esemény van | **RTC** | a körpuffer tele van friss adattal, ami időben mindenképp újabb |

A lap **kiírja, melyik forrásból** dolgozik, és ha a fájlé, azt is, mikor
mentettük: valós idővel dátumot, **anélkül pedig a mentés akkori uptime-ját**
(„mentve a bootolás után 3:12:45-kor"). Így a „melyik eseménysor mikori?"
kérdésre óra nélkül is van válasz, és nem találunk ki egy dátumot.
(Mérve: `NV5`, `NV11`.)

**Ha a fájl hiányzik, üres, csonka, rossz a magic-je vagy a fejléce többet ígér,
mint amennyi ott van** – mind az öt eset ugyanoda vezet: a lap az RTC naplót
mutatja, külön hibaüzenet nélkül. Ez nem kivételkezelés, hanem a normál működés
egyik ága. **Ha mindkettő üres**, egyszerűen nincs napló a lapon – nem
hibaüzenet, nem üres táblázat. (Mérve: `NV6`.)

> A `/log` kezelő **egyetlen** 384 bájtos puffert használ, mert az `async_tcp`
> task verme véges: előbb a fejlécből eldönti, melyik forrás kell, és csak azt
> tölti be. Ha a betöltés félúton bukik, a puffer fél fájl, fél RTC adat lenne –
> ezért ilyenkor **újra vesszük** az RTC pillanatképet, nem csak
> „visszalépünk". (Mérve: `NV8`.)

### Valós idő (NTP)

A napló eddig csak uptime bélyeget hordozott, ami minden induláskor nulláról
kezd – két bootolás eseményei így nem rendezhetők egymáshoz. Ez pontosan az,
ami a mentett napló értelmezéséhez kell.

| | |
|---|---|
| Szerver | `hu.pool.ntp.org` |
| Időzóna | `CET-1CEST,M3.5.0,M10.5.0/3` – magyar idő, a nyári időszámítás váltásaival |
| Indítás | a `loop()`-ból, amint van kapcsolat – **bármelyik úton** jött is létre; **nem blokkol** (a válasz a háttérben érkezik) |
| Újracsatlakozás után | újraindul (a `disconnect(true)` a netifet is lebontja), de a soros porton **csak egyszer** jelenti be |
| Amíg nincs szinkron | az `epoch` mező 0 marad, a lap `-`-t ír az Idő oszlopba – a napló ettől még működik |

### Ha az NTP kommunikáció nem sikerül

**Nem okoz gondot** – és ez nem feltevés, hanem szerkezeti tulajdonság: az órára
mindössze **két** dolog épül, és mindkettő működik nélküle is.

| Mire kell | Óra nélkül |
|---|---|
| A bejegyzések **kiírása** | `-` áll az Idő oszlopban; az uptime oszlop ilyenkor is elmond mindent |
| A frissesség-döntés **tie-breakje** | csak akkor használjuk, ha **mindkét** oldalnak van érvényes időbélyege; egyébként a darabszám-alapú szabályra esünk vissza |

Egyetlen ág sem **vár** az órára, és egyik sem hiúsul meg nélküle. A
`nowEpoch()` alsó korlátja (2025-01-01) gondoskodik arról is, hogy szinkron
nélkül **ne jelenjen meg hamis 1970-es dátum** – a mező marad 0, a lap pedig `-`
-t ír. A „mentve: …" címke ugyanígy egyszerűen elmarad, nem találunk ki egy
időpontot.

A `configTzTime()` maga sem blokkol: csak elindítja az SNTP klienst poll
módban, a válasz a háttérben (a tcpip taskban) érkezik – ha a szerver
elérhetetlen vagy a névfeloldás bukik, az a mi `loop()`-unkat nem érinti.

> A mérés: az `NV11` végigjátssza a teljes eszkalációt (tesztek bukása → router
> reset → mentés → alvás), majd egy áramszünet utáni `/log` oldalt is –
> **végig óraszinkron nélkül**. Mellékesen az **egész tesztkészlet** így fut: a
> `coldBoot()` `g_epochNow = 0`-t állít, tehát mind a 278 forgatókönyv a
> „nincs óraszinkron" állapotot játssza.

> A valós idő a **deep sleepet túléli**. Az `esp_timer` (és így a `millis()`)
> ébredéskor nulláról indul, a `gettimeofday()` alapú rendszeróra viszont az RTC
> órából jön – egy 1 órás alvás után is jó időt mutat. Épp ezért használható a
> bejegyzések rendezésére bootolásokon át.

> **Miért a `loop()`-ból, és nem az `initWiFi()`-ből?** Mert nem minden kapcsolat
> azon keresztül jön létre. Három ág kerülte volna meg: az `initWiFi()` „már
> csatlakozva vagyunk" korai visszatérése, a `handleFirstStart()` korai kilépése
> (a próba már igazolta a kapcsolatot – **ez a leggyakoribb helyreállási út
> áramszünet után**), és a `FAILURE_STATE` `RESET_DELAY` korai kilépése.
> Mindháromban elmaradt volna a szinkron, és a napló `epoch` mezője végig 0
> maradt volna – épp a mentett napló értelmezése romlott volna el. (Mérve:
> `NV10`, mutációval is ellenőrizve.)

### Hová és mikor íródik a napló (RTC)

**Az elsődleges napló nem fájl.** Nem megy a LittleFS-re, nincs `/log.txt`, nincs flash
írás. Egy 32 elemű körpuffer az RTC memóriában, amibe a `logEvent()` egyetlen
struktúra-értékadással ír. Ennek három következménye van, és mindhárom
szándékos:

- **Nem tud „naplóírási hibát" okozni.** Nincs mit kezelni: nincs megnyitás,
  nincs `write()` visszatérési érték, nem tud betelni a tár. Ami a naplózás
  miatt megakadhatna a programban, az itt eleve nem létezik.
- **Túléli azt is, amikor a fájlrendszer viszi el a hibát.** Ha a LittleFS
  csatolása bukik (`FATAL` 1-es paraméterrel), a napló *pont akkor* olvasható,
  amikor egy fájlba írt napló elveszne.
- **Nem koptatja a flasht.** Egy hosszú kiesés alatt óránként több tucat
  esemény keletkezhet; fájlba írva ez fölösleges írásciklus lenne.

A puffer két különböző taskból is íródik – a `loop()`-ból és az AsyncTCP
webszerver taskjából –, ezért minden írás és olvasás `portENTER_CRITICAL`
kritikus szakaszban történik. A **két időbélyeg viszont a szakaszon kívül**
készül el: a kritikus szakasz a C3-on letiltja a megszakításokat, tehát odabent
minden extra munka közvetlen költség – az uptime egy 64 bites osztás, a valós
idő pedig `time()`-ot hív, ami az ESP-IDF-ben a rendszeróra saját zárját is
megfoghatja. Idegen zárat felvenni letiltott megszakítások mellett nem az a
minta, amit egy naplózó függvénytől várunk; így odabent tényleg csak
értékadások maradnak. A szakaszok **nincsenek egymásba ágyazva**
(mérve: `LOG5`), így nem tudnak megakadni.

### Túlcsordulhat-e?

Két külön kérdés van benne, és mindkettőre nem a válasz.

**A körpuffer indexe.** Az írási pozíció (`rtcEvNext`) monoton nő és sosem
csökken, tehát elvben körbefordul a 32 biten. Az írás helye
`rtcEvNext % 32` – és mivel a 32 **kettő hatványa**, a 2^32 maradék nélkül
osztható vele: a `0xFFFFFFFF → 0x00000000` átmenetnél az index 31-ről 0-ra lép,
azaz pontosan ott folytatja, ahol tartott. Nincs kihagyott vagy kétszer írt
slot, és a maradékképzés miatt kiindexelni sem tud. (Ha a méret nem kettő
hatványa lenne – mondjuk 30 –, itt ugrana az index. Mérve: `LOG6`.)

**A számláló értéke.** A körbefordulás után a `/log` átmenetileg kevesebb sort
mutat, mint amennyi valójában a pufferben van (a legrégebbi keresése az
`evTotal`-ból indul). Ez egyszeri és kozmetikai – és 2^32 eseményt igényel. A
leggyorsabb tartós eseményütem a firmware-ben a router reset sorozat, ~20
esemény óránként; ezzel a fordulás **több tízezer év**. A `uptimeSec` mező
ugyanilyen nagyságrend: uint32 másodperc = 136 év folyamatos üzem.

A körbefordulás pillanatában a `lastEventWas()` (`rtcEvNext > 0` kapu) és a
`stuckCycleAlreadyLogged()` (`rtcEvNext < 2` kapu) egyszerűen `false`-t ad –
egyetlen kimaradt spamszűrés, nem hiba, és nem indexel ki a tömbből.

### Az ismétlődő események szűrése

Az ismétlődő eseményeket **csak az első alkalommal** írjuk be, különben egy
tartós hiba pár perc alatt kisöpörné a puffert, és épp a hiba *kezdetét*
veszítenénk el – azt, ami a diagnózishoz kell. Három helyen van ilyen szűrés:

| Esemény | A szűrés módja |
|---|---|
| `TEST FAIL` | csak `failedCount == 1`-nél |
| `STUCK BUTTON` | `stuckCycleAlreadyLogged` jelző |
| `WIFI LOST` | `lastEventWas(EV_WIFI_LOST)` – ha az előző bejegyzés is ez volt, kimarad |
| `LOW HEAP` | **kettős**: a `heapWarnActive` jelző a soros sort ritkítja (10% hiszterézissel), a naplóba pedig a `lastEventWas(EV_LOW_HEAP)` enged csak egyet |

> A `LOW HEAP` eredetileg **csak** a `heapWarnActive` jelzőn lovagolt, tehát
> minden küszöb-átlépés új naplóbejegyzést írt. Egy a küszöb körül ingadozó
> heapnél (amit a szokásos AsyncTCP puffer-forgalom előállíthat) ez **kisöpri**
> a 32 elemű körpuffert: mérve **30 perc alatt 32 bejegyzés, mind egymás után**.
> Ugyanaz a hibaosztály, mint a `WIFI LOST`-nál – csak ott észrevettük, itt nem.
> A javítás után ugyanez a mérés **1 bejegyzést** ad. (Mérve: `HP10`.)

A `WIFI LOST` szűrése volt a legutolsó hiányzó darab. **Az arányokról
őszintén:** védelem nélkül, valósághű pislákolási ütemek mellett (500 / 1000 /
2000 / 5000 ms) a mérés 4 / 2 / 1 / 0 bejegyzést adott – tehát a 32-es puffert
nem söpörte el. A mechanizmus viszont valós, és a pislákolás gyorsulásával
arányosan romlik; a védelem hat sor, és nincs hátránya. (Mérve: `LOG4`.)
