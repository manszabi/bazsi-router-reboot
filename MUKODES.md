# Működési táblázat

Az eszköz teljes viselkedése: mikor mit csinál és meddig. Minden érték a
`bazsi_router_reboot.ino` konstansaiból származik.

---

## 1. Üzemmódok

Az eszköz mindig pontosan egy üzemmódban van.

| Üzemmód | Mikor | Mit csinál |
|---|---|---|
| `MODE_MONITOR` | Van Wi-Fi kapcsolat, vagy épp épül | Internetet tesztel, szükség esetén routert indít újra |
| `MODE_CONFIG` | Nincs használható Wi-Fi konfiguráció | AP beállító portál (`ESP-<chip>-XXXX`, eszközönként egyedi jelszó – a soros porton, `192.168.4.1`) |
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
| **Van** | **legfeljebb 10 perc** várakozás (`firstStartDelay`), majd **3 próba** (köztük 30 mp). A várakozás korábban véget ér, ha a percenkénti próbánál a hálózat **és** az internet is megvan – lásd lent |

A 3 próba után a **hiba oka** dönt:

| `WiFi.status()` | Jelentés | Következmény |
|---|---|---|
| `WL_CONNECT_FAILED` | hitelesítési hiba → rossz jelszó | **Azonnal AP mód** |
| bármi más | a hálózat nem látszik | a **Wi-Fi-lépcső** (5. fejezet) – hidegindulásnál az ESP-újraindítás kimarad, az első lépés a router reset |

Az ESP32 core megkülönbözteti a kettőt (`STA.cpp`): `WIFI_REASON_NO_AP_FOUND` →
`WL_NO_SSID_AVAIL`, `WIFI_REASON_AUTH_FAIL` → `WL_CONNECT_FAILED`. Konzervatívan
döntünk: **csak** az explicit hitelesítési hiba küld AP módba, mert a téves
„várjunk tovább" ára késleltetett újrakonfigurálás, a téves „AP mód" ára viszont
egy halott eszköz. A rossz jelszót a core csak a 2. próbától jelzi (`R5`).

A 10 perces várakozás oka: áramszünet után az ESP másodpercek alatt elindul,
a router viszont percekig bootol. Hidegindulástól az első router resetig mérve
**12 perc** (`WF2`).

### Mekkora áramszünetet él túl felügyelet nélkül?

Teszttel kimérve (`PO1`–`PO3`):

| A router ennyi idő múlva áll fel | Eredmény |
|---|---|
| 8 perc | Kivárja a türelmi időben, csatlakozik |
| 11 perc | Az első 3 próba elkapja |
| 20 perc | A router reset utáni várakozás elkapja |
| ennél tovább | a Wi-Fi-lépcső: router reset 30 perc / 1 óra / 2 óra / 4 óra szünettel, közben percenként próba |
| **48 óránál tovább** | Feladja: AP mód, majd 5 perc után alvás |

**A hosszú várakozások percenkénti próbája** (`onlineProbe()`) két lépésből
áll, ebben a sorrendben:

1. **Wi-Fi – kísérlet, nem teszt.** Kapcsolat híján csak *kezdeményez* egyet
   (aszinkron `WiFi.begin()`, nem vár rá), és kilép. A kudarcnak **nincs
   következménye**: nem nő számláló, nem változik állapot.
2. **Internet – csak meglévő kapcsolat mellett.** Ugyanaz az ellenőrzés, mint
   normál üzemben (3. fejezet). Hálózat nélkül el sem indul.

A várakozás akkor zárul korábban, ha **mindkét lépés** sikerül. A próba
**ismétlődik**, tehát elég, ha a kapcsolat a várakozás *bármely* pontján
helyreáll (mérve: `OP7`).

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

| Állapot | Mit csinál | Meddig |
|---|---|---|
| `CHECK_STATE` | megnézi, van-e internet; kiesésnél a router pingjét is | ~1 mp (élő internet) – ~50 mp (halott DNS) |
| `WAIT_STATE` | vár a következő ellenőrzésig | a kör **kezdetétől** 1 perc (`CHECK_INTERVAL_MS`) |
| `RESET_STATE` | vár a router bootolására | legfeljebb 10 perc (`RESET_DELAY`) |

**Van internet**, ha az alábbiak közül bármelyik sikerül (a sor az első
sikernél megáll):

| # | Mit | Időtartam, ha nem sikerül |
|:-:|---|---|
| 1 | ping `1.1.1.1` (4 próbából 2 válasz kell) | ~5 mp |
| 2 | ping `8.8.8.8` | ~5 mp |
| 3 | HTTP teszt a soron következő végpontra (Microsoft, Cloudflare, Mozilla, GNOME, Google) | 15 mp (a szerver hallgat), 28–33 mp (halott DNS) |

A HTTP teszt csak akkor fut, ha mindkét ping hallgat – vagyis akkor, ha a
hálózat a pinget szűri. A 204-es végpontok captive portált sem fogadnak el.
Sikernél a soros portra csak a `Successful Test` kerül; eltéréskor a kapott
törzs és a `Hamis ertek!` is.

---

## 4. Az internet kiesik – router újraindítás

Kiesésnél minden körben a router (gateway) pingje is lefut. Ez dönti el, mennyi
internet nélküli idő után jön a reset:

| `NET DIAG` | Helyzet | Reset |
|:---:|---|---|
| **1** | a router **sem** válaszol (vagy nincs Wi-Fi / gateway-cím) | **2 perc** után (`ROUTER_DOWN_WINDOW_MS`); 22:00–05:00 között **10 perc** (`QUIET_WINDOW_MS`) |
| **2** | a router válaszol, az internet nem | **15 perc** után (`WAN_DOWN_WINDOW_MS`) |

Ha a reset nem segített, a következő az előzőhöz képest **30 perc → 1 óra →
2 óra → 4 óra** múlva jön, onnan 4 óránként (`RETRY_BACKOFF_MS`). Egy sikeres
ellenőrzés mindent nulláz. Alvás ezen az ágon nincs: az eszköz végig figyel, és
naplózza, amikor visszajön az internet.

A Wi-Fi elvesztésére (3 próba után sincs kapcsolat) a Wi-Fi-lépcső felel
(5. szakasz): előbb az ESP indul újra, és csak ha az sem segít, jön a router
reset. Rossz jelszónál reset helyett AP mód.

**Éjszakai csendes időszak** (`QUIET_START_HOUR` = 22, `QUIET_END_HOUR` = 5,
helyi idő a `NTP_TZ` szerint): a router-hiba (`NET DIAG` 1) türelmi ideje
2 helyett 10 perc, és a Wi-Fi miatti első router reset is csak legalább 10 perc
kiesés után jön (`wifiResetWaitMs()`), hogy egy éjszakai firmware-frissítést ne
szakítson félbe. A szolgáltatói hiba (15 perc), a DNS-ág és a lépcső többi foka
változatlan. Érvényes idő nélkül (áramszünet után, NTP előtt) a nappali szabály
él. Az időzónát a `setup()` maga állítja be (`applyTimezone()`), mert az
újraindulás után az NTP csak Wi-Fi-vel indulna, az óra viszont az RTC-ben
megmaradt; e nélkül éjjel UTC-ben számolna, és a napló időbélyegei is UTC-ben
jelennének meg.

### Mennyi idő alatt dönt? (mérve)

| Helyzet | Eredmény | Teszt |
|---|---|---|
| router hiba, az első hibás ellenőrzéstől a resetig | **150 mp** (halott DNS: 168 mp) | `R7` |
| 5, 10, 14 perces szolgáltatói kiesés | **0** reset | `RD2` |
| 6 órás szolgáltatói kiesés | **4** reset: 15 perc, majd +30 perc, +1 óra, +2 óra | `RD3` |
| egy napig halott router | **9** reset, köztük legalább 30 perc | `RD8` |
| a 2026-11-22-i terepi nap (élő internet, lassú HTTP) | **0** reset (a régi firmware: 4) | `RD4` |
| a hálózat a pinget szűri, a HTTP megy | **0** reset | `RD6` |
| a router pingre sosem válaszol, szolgáltatói kiesés | reset már ~2,5 perc után (a 15 helyett) | `RD5` |

### A reset menete

| Lépés | Időtartam |
|---|---|
| Relé **be** → router áramtalanítva, státusz LED **villog 2 Hz** | |
| Áramtalanítás | **90 mp** (`RESET_PULSE`) |
| Relé **ki** → router visszakap áramot | |
| Várakozás a bootra | **legfeljebb 10 perc**; percenként megnézi, visszajött-e a hálózat és az internet, és ha igen, azonnal véget ér |
| Wi-Fi újracsatlakozás (ha a várakozás nem ért véget korán) | 3 próba, köztük 30 mp |

### A router DNS-e

Ha a ping megy, 5 percenként (`DNS_CHECK_INTERVAL_MS`) egy DNS-kérdés megy
**közvetlenül a router 53-as portjára** (saját UDP-csomag: az lwIP
névfeloldása átváltana a tartalékra és gyorsítótárazna).

| Router DNS | Kontroll: `1.1.1.1` DNS | Mi történik |
|---|---|---|
| válaszol (hibakód 0 vagy 3) | – | rendben; a DNS-számláló nullázódik |
| hallgat (vagy SERVFAIL, REFUSED) | válaszol | **gyanú**: percenként újra kérdez; **10 perc** (`DNS_FAIL_WINDOW_MS`) után reset, `NET DIAG` 3 |
| hallgat | hallgat | nincs reset (nem a router hibája) |
| a DHCP nem a routert adja DNS-nek | – | az ellenőrzés kimarad (egyszer kiírja) |

Egy kérdés: 2 próba, 3 mp várakozás (`DNS_ATTEMPTS`, `DNS_TIMEOUT_MS`), tehát
legfeljebb 6 mp; a kontrollal együtt 12 mp. Ha a reset nem segít, a következő
30 perc → 1 óra → 2 óra → 4 óra múlva jön. Ennek **saját számlálója** van
(`dnsState.resetEvents`), mert a ping közben végig sikeres, és a fő lépcsőt
minden sikeres ping nullázza.

| Helyzet (mérve) | Eredmény | Teszt |
|---|---|---|
| a router DNS-e lefagy | reset legkésőbb ~16 perc múlva (legfeljebb 5 perc a felismerésig + 10 perc kitartás) | `DN1` |
| a külső DNS is hallgat | 0 reset | `DN2` |
| 9 perces DNS-zavar | 0 reset | `DN3` |
| a 11-22-i nap, a zavarok alatt a DNS is hallgat | 0 reset | `DN4` |
| egy napig rossz router-DNS | 8 reset, köztük legalább 30 perc | `DN5` |

**Nem veszi észre:** ha a router a gyorsítótárából még válaszol, de továbbítani
már nem tud.

### A terepi napló (2026-11-22) tanulsága

A régi firmware HTTP alapján döntött. Aznap nappal a HTTP válaszok 10–12 mp
után időtúllépéssel elmaradtak, miközben az internet működött (a felhasználó
böngészett): ez négy téves resetet adott. A ping ilyenkor válaszol, ezért az
új szabállyal nincs reset. A csak DNS-fagyást a fenti DNS-ellenőrzés fogja meg.

### Statikus IP: ha a saját gateway sem válaszol

Statikus IP mellett a Wi-Fi **társítás sikerülhet** úgy is, hogy IP szinten
nincs út sehová – jellemzően **elgépelt statikus IP** miatt, amit a router
újraindítása nem javít.

| | |
|---|---|
| A gateway válaszol | a fenti szabályok |
| Nem válaszol, **először** | router hiba: reset (`GW UNREACH` 1) |
| A reset után **sem** válaszol | **AP beállító mód**, hogy javítható legyen (`GW UNREACH` 2, `AP MODE` 4) |

> Ha a routered nem válaszol ICMP echóra, statikus IP mellett ez tévesen
> „elérhetetlen"-t ad – ilyen routernél használj DHCP-t.

### Ha az újracsatlakozás sem sikerül

A reset után 3 próba; ha egyik sem sikerül: a Wi-Fi-lépcső (5. fejezet).

---

## 5. Wi-Fi kapcsolat megszakad – a Wi-Fi-lépcső

Működés közben a kiesést a percenkénti ellenőrzés veszi észre (Wi-Fi LED **ki**,
a státusz LED marad **be**). Utána:

| Fok | Mikor | Mit csinál |
|:-:|---|---|
| 0 → 1 | 3 újracsatlakozási próba (köztük 30 mp) sem segít | **az ESP újraindítja magát**, a router érintetlen; utána nincs 10 perces türelmi idő |
| 1 → 2 | az újraindulás után ismét 3 próba sem segít | **router reset** (`NET DIAG` 1), legfeljebb 10 perc várakozás a bootra, utána 3 próba; 22:00–05:00 között csak legalább 10 perc kiesés után |
| 2 → 3 … | még mindig nincs Wi-Fi | **30 perc**, majd **1 óra**, **2 óra**, onnan **4 óránként** újabb router reset |
| – | **48 órája** nincs Wi-Fi (`WIFI_GIVEUP_MIN`) | **AP beállító mód** (`AP MODE` 3) |

- **Rossz jelszó** (`WL_CONNECT_FAILED`) bármelyik fokon: azonnal AP mód,
  újraindítás és reset nélkül.
- **A várakozás ébren történik**: közben percenként egy csatlakozási próba fut
  (nem három), és ha a Wi-Fi visszajön, azonnal folytatódik a figyelés.
- **Hidegindulásnál** (áramszünet után) a 0. fok kimarad: az ESP épp most indult.
- **Ha a kiesés már kapott router resetet** (pl. a router sem válaszolt, a reset
  után pedig nem jött vissza a Wi-Fi), az ESP ugyanúgy újraindul, de a lépcső
  onnan folytatódik: a következő reset az előző után 30 perccel (majd 1 / 2 /
  4 óra) jön, nem rögtön az újraindulás után (`WFL11`, `WFL12`; korábban ~15 perc volt).
- **Nullázás**: ha a Wi-Fi **30 percig** folyamatosan megvan (`WIFI_STABLE_MS`).
  Így egy instabil routernél a második kiesés már router resetet kap, nem újabb
  ESP-újraindítást (`WFL5`).

**Mit őriz meg az újraindulás:** a fokot és a kiesés eddigi hosszát
és az utolsó router reset óta eltelt percet (`rtcWifiStep`, `rtcWifiOutageMin`,
`rtcWifiResetAgoMin`, `RTC_NOINIT`, ellenőrző értékkel). Indulás után a
2+. fok az internet-ág reset-számlálóját is beállítja, így ha a Wi-Fi visszajön,
de az internet nem, ott sem jön idő előtt újabb reset. Ez a
saját, a heap miatti és a watchdog-újraindulást túléli. **Törli** az
áramszünet, a reset és a wifireset gomb, a mentés utáni újraindulás és minden
alvás – ezek emberi beavatkozások, tehát tiszta lap.

| Mérve | Eredmény | Teszt |
|---|---|---|
| az újraindítás után visszajön a Wi-Fi | nincs router reset, azonnal figyel | `WFL1` |
| a Wi-Fi elvesztésétől a router reset kezdetéig | **5,3 perc**; éjjel (22:00–05:00) **15,3 perc** | `WFL2`, `QH6` |
| tartós Wi-Fi-hiány | resetek 30 / 60 / 120 / 240 perc szünettel (+ a bootvárakozás), 48 óra után AP, alvás nélkül | `WFL3` |
| várakozás közben visszajön | legfeljebb 1 perc alatt észreveszi; percenként 1 próba | `WFL4` |

### Ha te magad áramtalanítod a routert

**Az eszköz ezt nem tudja megkülönböztetni attól, hogy a router lefagyott** – a
hálózat mindkét esetben ugyanúgy eltűnik.

| | mért érték |
|---|---|
| **Türelmi idő** a hálózat eltűnésétől az ESP saját router-resetjéig | **4,3 perc** (`PWR3`; közben az ESP egyszer újraindul); 22:00–05:00 között legalább 10 perc |
| Ha a router ezen belül visszajön | az ESP **hozzá sem nyúl** a reléhez, csendben visszacsatlakozik (`PWR4`) |

---

## 6. AP beállító mód

| Esemény | Időtartam |
|---|---|
| Portál elérhető: `192.168.4.1` | |
| Jelzés: **Wi-Fi LED villog 1 Hz**, státusz LED végig **be** | azonnal |
| Az űrlap előkitöltése | SSID, IP, gateway a mentett értékkel (HTML-escape-elve); **a jelszó soha** |
| A beállító űrlap forrása | a programba fordítva (`FORM_HEAD` / `FORM_TAIL` + `sendConfigForm()`), nincs feltöltendő `data/` mappa – a portál a LittleFS-ről semmit nem szolgál ki |
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
> **10 ms-onként** olvassa mindkét gombot – a `waitWithButtons()`, az
> `initWiFi()` 20 mp-es várakozása, a DNS-válaszra várás, a reset
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

> **A Wi-Fi LED legfeljebb egy percet késhet:** a kapcsolatot a percenkénti
> ellenőrzés nézi, két ellenőrzés között nem (mérve: `LED4`, 58 mp).

---

## 12. Heap felügyelet

A sketch **saját kódja** nem allokál dinamikusan – ezt a `make lint` fordítási
idő előtt ki is kényszeríti (tilos a `malloc`, a `new`, a dinamikusan foglaló
STL tároló és az érték szerinti `String`).

> **Pontosítás, mert a korábbi „semmit nem allokál" túlzás volt.** Néhány
> *könyvtári* hívás, amit a program tesz, `String`-et ad vissza érték szerint,
> és az Arduino `String` mindig a heapről dolgozik:
> `http.begin(client, url)` (a `const char*`-ból ideiglenes `String` lesz),
> `http.header("Transfer-Encoding")` és `WiFi.SSID()`. Ezek rövid életű,
> teszt-ciklusonként néhány tíz bájtos foglalások – de attól még foglalások,
> és a töredezettséghez hozzátesznek. A saját pufferek fix méretűek; a heapet
> az `ESPAsyncWebServer`, az `AsyncTCP`, a `HTTPClient` és a Wi-Fi driver
> használja érdemben.

Egy lassú szivárgás vagy elaprózódás
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

Azt, hogy „az első fázis már lefutott", a `currentState == RESET_STATE` és a
`timing.stateStart` hordozza. Egy újraindulás mindkettőt elveszti – a
`timing.stateStart`-ot **átvinni sem lehetne** értelmesen, mert a `millis()`
ébredéskor nulláról indul.

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
lefut (és a próba perceken belül le is zárja).

**Egy kivétel van:** a `testState.resetEvents`, a reset-lépcső állása. Ha egy
heap-újraindulás nullázná, a következő reset a 30+ perces lépcső helyett már
2–15 perc múlva jönne. Ezért átmegy az RTC memórián (`rtcCarryResetEvents`), és
a `setup()` **pontosan egyszer** használja fel; ha nem nulla, a kiesés
folytatódik, és a következő reset a lépcső szerint az indulástól számít.
(Mérve: `HP4`, `RD9`.)

Ami eleve túléli: a diagnosztikai napló és a watchdog-számláló (`RTC_NOINIT`),
valamint a LittleFS-en tárolt konfiguráció.

#### A Wi-Fi-lépcső

A Wi-Fi-lépcső foka és a kiesés hossza eleve `RTC_NOINIT`-ben él (5. fejezet),
tehát külön átvitel nélkül is túléli a heap-újraindulást; előtte a kiesés eddigi
hossza percre pontosan mentődik. Gombnyomás viszont törli: ott tiszta lap kell.
(Mérve: `WFL8`, `GWH5`.)

#### A teljes lista: mi vész el, és miért nem baj

| Változó | Mi lesz vele | Miért rendben |
|---|---|---|
| `testState.resetEvents` | **átvisszük** | enélkül a reset-lépcső elölről kezdődne |
| `rtcWifiStep`, `rtcWifiOutageMin`, `rtcWifiResetAgoMin` (Wi-Fi-lépcső) | eleve `RTC_NOINIT`, a kiesés hossza előtte mentődik | lásd fent |
| `testState.outage`, `outageStart`, `lastResetAt` | újraszámolódik | ha volt reset, a kiesés az indulástól folytatódik (`GWH3`) |
| `testState.endpointIndex` | elvész | csak azt mondja meg, melyik HTTP végpont jön |
| `dnsState` (DNS-gyanú, DNS-lépcső) | elvész | a DNS-ellenőrzés újraindul; legfeljebb egy korábbi DNS-reset jön |
| `testState.resetStep` | elvész | **nem lehet** félbehagyott pulzus: a relé impulzusa alatt nincs újraindulás |
| `currentState`, `timing.stateStart` | elvész | **ezért van az ellenőrző ablak kizárása** – lásd fent |
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
| `rtcWdtResets`, `rtcWifiStep`, `rtcWifiOutageMin` | **csak olvas** (`/log`) | ír | igazított szó – a lapon legfeljebb egy pillanattal régi érték áll |
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
| Normál működés | 7 sor/perc (`SER1`) |
| Tartós internetkiesés | 19 sor/perc (`SER2`) |
| AP mód, végzetes hiba (villogó ciklus) | **0** a ritkított heap-soron kívül (`SER3`) |
| **Pislákoló Wi-Fi** (0,7–4,7 mp) | 8–13 sor/perc (`LOG4`) |
| **A küszöb körül ingadozó heap** | 13 sor/perc (`SER8`) |
| **Mindig bukó naplómentés** tartós kiesés alatt | 22 sor/perc (`SER9`) |

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
| `WIFI OK` | a Wi-Fi-lépcső foka csatlakozáskor: 0 = nem volt kiesés, 1 = az ESP újraindítása segített |
| `WIFI RESTART` | az ESP a Wi-Fi miatt indult újra; a param a `WiFi.status()` értéke |
| `WIFI LOST` | `WiFi.status()` a kiesés pillanatában (csak a kiesés-sorozat **első** tagja) |
| `TEST FAIL` | a kiesés kezdete (kiesésenként **egyszer**); a param a bukott tartalék HTTP teszt sorszáma, 1–5 |
| `HTTP ERROR` | a tartalék HTTP teszt kódja, közvetlenül a `TEST FAIL` után: 200/204/403/5xx változatlan (200 = jött válasz, de nem a várt szöveg); kliens hibánál 1000 + \|kód\|: **1001** = nem sikerült kapcsolódni vagy a nevet feloldani, **1011** = nem jött válasz időben, **1100** = a kérés el sem indult |
| `NET DIAG` | a router reset oka, közvetlenül a reset előtt: 1 = a router sem válaszol (vagy nincs Wi-Fi / gateway), 2 = a router válaszol, az internet nem, 3 = az internet él, de a router DNS-e 10 perce hallgat |
| `ROUTER RESET` | hányadik reset ebben a kiesésben |
| `AP MODE` | 1 = nincs mentett SSID, 2 = hitelesítési hiba, 3 = 48 órája nincs Wi-Fi, 4 = a gateway sem érhető el |
| `GW UNREACH` | 1 = a router reset előtt, 2 = a reset után is |
| `CONFIG SAVED` | 0 |
| `SLEEP` | 3 = AP időtúllépés, 4 = végzetes hiba |
| `FATAL` | 1 = LittleFS csatolás, 2 = konfiguráció olvasás, 3 = watchdog, 4 = a wifireset törlése nem sikerült, 5 = tartósan kevés a heap |
| `WDT RESET` | hányadik rendellenes újraindulás |
| `STUCK BUTTON` | 0 = reset gomb, 1 = wifireset gomb (csak az **első** kör – az ismétlődő 60 mp-es alvás-ébredés körök nem íródnak be újra) |
| `LOW HEAP` | a szabad heap **KB**-ban a küszöb átlépésekor (csak az átlépés, lásd a 12. fejezetet) |
| `HEAP RESTART` | a szabad heap **KB**-ban az önkéntes újraindulás előtt |

A `/log` oldal az aktuális állapotot is mutatja: **az indulás oka szövegesen**
(a nyers enum-szám zárójelben marad, hibajelentéshez), watchdog számláló,
a Wi-Fi-lépcső foka és a kiesés hossza, és az **uptime nap/óra/perc/mp alakban** – ugyanúgy,
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
| **Heap miatti önkéntes újraindulás előtt** | vezérelt leállás, mint az alvás. `ESP.restart()` az RTC memóriát nem törli, de a bejegyzések így csak a *következő* mentési pontig élnének kizárólag RTC-ben – egy áramszünet abban az ablakban épp azt a bizonyítékot vinné el, amiért az újraindulás történik (mérve: `RST1`) |
| **Wi-Fi miatti saját újraindulás előtt** | ugyanez az ok: a kiesés előzménye ne csak RTC-ben éljen (mérve: `WF6`) |

Az alvások mentése **egyetlen közös ponton**, az `enterDeepSleep()` elején
történik – így mindkét alvási út (`apSleep`, `fatalSleep`) megkapja, és egy **új** alvási úttól sem lehet elfelejteni. (Mérve: `NV9`.)
Csatolatlan fájlrendszernél a mentés magától kimarad, és nem akasztja meg az
alvást.

**A fájl SAJÁT, nagyobb gyűrű – nem az RTC napló pillanatképe.** A mentés
**hozzáfűz**: a fájl megőrzi a régi tartalmát, és a végére kerül az, ami a
legutóbbi sikeres mentés óta keletkezett (`rtcSavedEvNext`). A fájl így
**128** bejegyzést őriz – négyszeresét az RTC gyűrűnek –, kiegyenesítve (a
legrégebbitől a legújabbig), így az olvasónak nem kell tudnia, hol tartott a
gyűrű.

> **Korábban felülírt, és pontosan az áramszünetnél tette a legrosszabbat.**
> Az `RTC_NOINIT` terület ilyenkor törlődik, tehát a „meddig mentettünk" jelző
> is nullázódik – és az első mentés az **egyetlen friss, még időbélyeg nélküli
> `BOOT`** bejegyzést írta a korábbi 32 helyére. Mérve: 404 bájt / 32
> bejegyzés → 32 bájt / 1. Vagyis épp az az esemény tüntette el a tartós
> naplót, amiért az egyáltalán létezik. (Mérve: `NV20`, `NV21`, `NV22`.)

A **verziószám nem változott**: az elrendezés ugyanaz maradt, csak a `count`
mező vehet fel nagyobb értéket. Egy **régi**, legfeljebb 32 bejegyzéses fájlt
az új firmware változatlanul beolvas, és az első mentésnél kiegészít – a
firmware-frissítés nem dob el történetet.

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
> esemény, nem írunk újra. **Mérve**: működő internet mellett 67,9 perc alatt
> **nulla** flash-írás; egy napig halott internetnél 18 írás (9 reset, resetenként
> egy mentés × 2 írás). (`WEAR`.) A LittleFS a tényleges törlési ciklusszámot nem
> adja vissza az Arduino API-n, tehát a kopás közvetlenül nem olvasható ki –
> az írások száma az egyetlen mérhető közelítés. A `wifireset` gomb a négy konfigurációs fájlt törli,
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

### Mit mutat az AP mód weboldala?

**Mindkét forrást, egymás után – nem egymás helyett.** Amíg a fájl az RTC napló
pillanatképe volt, a kettő átfedte egymást, és a lapnak *választania* kellett
(időbélyeg-összehasonlítással, azonos időbélyegnél darabszám alapján). Mióta a
mentés **hozzáfűz**, a két forrás kiegészíti egymást:

1. előbb a **fájl** teljes tartalma (a hosszú történet, max 128 sor),
2. utána az **RTC**-ből az, ami a legutóbbi sikeres mentés óta keletkezett.

Kettőzés nincs, és **a „melyik a frissebb?" heurisztikára sincs többé szükség**.

> **A sorrend szerkezeti, nem időbélyeg-alapú.** A fájl bejegyzései a
> legrégebbitől a legújabbig állnak, az RTC új része pedig definíció szerint
> utánuk következik. Ezért **NTP nélkül is helyes a sorrend** – ami azért
> számít, mert óra épp a legrosszabb esetben (friss bootolás áramszünet után)
> nincs.

A táblázat **`Forrás` oszlopa** minden sornál kiírja, `fajl` vagy `RTC`. A
fejléc pedig azt is, mikor mentettünk utoljára: valós idővel dátumot,
**anélkül pedig a mentés akkori uptime-ját** („utoljára mentve a bootolás után
3:12:45-kor"). Így a „melyik eseménysor mikori?" kérdésre óra nélkül is van
válasz, és nem találunk ki egy dátumot. (Mérve: `NV5`, `NV11`, `NV14`.)

> ⚠️ **A „meddig mentettünk" jelző csak akkor mond igazat, ha a fájl megvan.**
> Ha elveszett (LittleFS újraformázás, törölt fájl, flash-hiba), a jelző
> elrejtené azokat az RTC bejegyzéseket, amikről azt *hisszük*, hogy már mentve
> vannak – mérve **13 meglévőből 3** látszott. És fordítva: áramszünet után a
> jelző nulláról indul, miközben a fájl egy régebbi „életből" való nagyobb
> `evNextAtSave`-et hordoz; ha azt hinnénk el, a friss bejegyzéseket rejtenénk
> el. A helyes alap ezért a kettő **kisebbike**, fájl híján nulla – így sem
> elrejteni, sem kettőzni nem tud. (Mérve: `GAP`, `GAP2`.)

**Ha a fájl hiányzik, üres, csonka, rossz a magic-je vagy a fejléce többet ígér,
mint amennyi ott van** – mind az öt eset ugyanoda vezet: a lap az RTC naplót
mutatja, külön hibaüzenet nélkül. Ez nem kivételkezelés, hanem a normál működés
egyik ága. **Ha mindkettő üres**, egyszerűen nincs napló a lapon – nem
hibaüzenet, nem üres táblázat. (Mérve: `NV6`.)

> **A verem és a heap.** A `/log` kezelő az `async_tcp` taskon fut, aminek a
> verme véges: a fájlt ezért **nyolcasával** olvassa (96 bájt puffer), nem
> egészben (128 bejegyzés 1536 bájt lenne). A válasz-stream kezdő puffere
> viszont a **tényleges tartalomhoz** méretezett (`~2600 + sorok × 120` bájt),
> mert a könyvtár `resizeAdd()`-je pontosan a hiányzó bájtokkal növel –
> soronként újraallokálna. Mérve: a teljes napló (160 sor) **18 070 bájt**, a
> becsült puffer 21 800. A valódi eszközön a legnagyobb összefüggő tömb
> **114 676 bájt**, tehát ez annak a 18%-a, és a lapot ember nyitja meg,
> ritkán. (Mérve: `LOG7`.)

### Soros parancsok

A `/log` lap csak **AP módban** érhető el. Egy normálisan működő eszköztől a
soros kábel az egyetlen út a naplóhoz – és a lapot amúgy sem szabad korlátlanul
növelni. Ezért van két parancs:

| Parancs | Mit csinál |
|---|---|
| **`LOG`** | a **teljes** napló: előbb a fájl, utána az RTC-ből a mentés óta keletkezett rész |
| **`HELP`** | a parancsok listája |

**Nem blokkol**: csak a már megérkezett bájtokat dolgozza fel, egy félig beírt
sor a következő `loop()` iterációban folytatódik – a watchdog-etetés rendje sem
változik. `MODE_FATAL`-ban is működik, mert a `loop()` fatal ága korábban
visszatér, a parancsfeldolgozás viszont **előtte** áll.

> A **túl hosszú sort egészben eldobjuk**, a sorvégig. Az első változat a
> puffert a túlcsordulásnál nullázta, de utána újra gyűjteni kezdett – vagyis a
> hosszú sor *végéből* lett egy csonkolt parancs. (Mérve: `CMD1`, `CMD2`,
> `CMD3`.)

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
mindössze **egy** dolog épül, és az is működik nélküle.

| Mire kell | Óra nélkül |
|---|---|
| A bejegyzések **kiírása** | `-` áll az Idő oszlopban; az uptime oszlop ilyenkor is elmond mindent |

**A sorrendhez már nem kell óra.** Amíg a fájl az RTC napló pillanatképe volt,
a lapnak el kellett döntenie, melyik forrás a frissebb – és ehhez időbélyeget
hasonlított, tie-breakkel. Mióta a mentés hozzáfűz, a sorrend **szerkezeti**: a
fájl a régebbi, az RTC új része követi. Ez azért fontos, mert óra épp a
legrosszabb esetben – friss bootolás egy áramszünet után – nincs.

Egyetlen ág sem **vár** az órára, és egyik sem hiúsul meg nélküle. A
`nowEpoch()` alsó korlátja (2025-01-01) gondoskodik arról is, hogy szinkron
nélkül **ne jelenjen meg hamis 1970-es dátum** – a mező marad 0, a lap pedig `-`
-t ír. Az „utoljára mentve: …" címke ugyanígy egyszerűen elmarad, nem találunk
ki egy időpontot.

A `configTzTime()` maga sem blokkol: csak elindítja az SNTP klienst poll
módban, a válasz a háttérben (a tcpip taskban) érkezik – ha a szerver
elérhetetlen vagy a névfeloldás bukik, az a mi `loop()`-unkat nem érinti.

> A mérés: az `NV11` végigjátssza a teljes eszkalációt (tesztek bukása → router
> reset → mentés → alvás), majd egy áramszünet utáni `/log` oldalt is –
> **végig óraszinkron nélkül**. Mellékesen **szinte az egész tesztkészlet** így fut: a
> `coldBoot()` `g_epochNow = 0`-t állít, tehát a forgatókönyvek döntő többsége a
> „nincs óraszinkron" állapotot játssza.

> A valós idő a **deep sleepet túléli**. Az `esp_timer` (és így a `millis()`)
> ébredéskor nulláról indul, a `gettimeofday()` alapú rendszeróra viszont az RTC
> órából jön – egy 1 órás alvás után is jó időt mutat. Épp ezért használható a
> bejegyzések rendezésére bootolásokon át.

> **Miért a `loop()`-ból, és nem az `initWiFi()`-ből?** Mert nem minden kapcsolat
> azon keresztül jön létre. Három ág kerülte volna meg: az `initWiFi()` „már
> csatlakozva vagyunk" korai visszatérése, a `handleFirstStart()` korai kilépése
> (a próba már igazolta a kapcsolatot – **ez a leggyakoribb helyreállási út
> áramszünet után**), és a `RESET_DELAY` korai kilépése.
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
| `TEST FAIL` | csak a kiesés első hibás ellenőrzésénél (`testState.outage`) |
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

---

## 16. A forráskód szerkezete

A program **nyolc fordítási egység**: a `bazsi_router_reboot.ino` és hét modul
a sketch mappájában. Összesen ~3550 sor, 123 függvény. A kommentek egysorosak
és csak a nem magától értetődő részeknél állnak; a döntések indoklása ebben a
dokumentumban és a commit-üzenetekben van.

> Miért nem egy fájl? Mert egy nagy `.ino`-ban nincs fordító által
> kikényszerített láthatóság: bármely függvény hozzáérhet bármely globálishoz.
> Külön fordítási egységekben viszont amit a header nem hirdet meg, azt a
> **linker sem találja meg**. Ez nem elmélet: a `netprobe` kiemelésekor a
> fordító azonnal jelzett két függőséget (`trimInPlace`, `waitWithButtons`),
> amit a kézi átvizsgálás nem vett észre.

### A gerinc: három üzemmód, három állapot

| | |
|---|---|
| **`deviceMode`** | `MODE_MONITOR` (a routert figyeli) · `MODE_CONFIG` (AP portál) · `MODE_FATAL` (megállt, villog) |
| **`currentState`** | `CHECK_STATE` → `WAIT_STATE` → vissza · vagy `CHECK_STATE` → `RESET_STATE` (router reset) → vissza |

A `MODE_MONITOR`-ból van út a másik kettőbe, **visszaút nincs**.

### A modulok

Fentről lefelé: minden modul csak a nála lentebb állóktól függ. A `configstore`
és az `eventlog` közti egyetlen közös pont a fájlrendszer állapota.

| Modul | Sor | Mit birtokol | Mit exportál |
|---|---:|---|---|
| **`limits_config.h`** | 8 | a konfig mezők méretkorlátai | 3 konstans, se állapot, se függvény |
| **`strutil`** | 24 | `trimInPlace()` | 1 függvény |
| **`secret`** | 86 | a jelszó elfedése a flashben | `encodeSecret()`, `decodeSecretInPlace()` |
| **`sync`** | 88 | a két task közti osztott állapot | 7 függvény + `evLogMux`, `apDeadline` |
| **`configstore`** | 230 | a négy mentett érték, a fájljaik, a LittleFS | a négy puffer, `readConfigValue()`, `readSecretValue()`, `writeConfigValue()`, `clearConfigValue()`, `staticPairValid()`, `initLittleFS()`, `filesystemReady()` |
| **`eventlog`** | 517 | a diagnosztikai napló **és** a valós idő (NTP) | `logEvent()`, `saveEventLog()`, `loadEventLogHeader()`, `loadEventLogRange()`, `printEventLogs()`, `eventName()`, `formatEpoch()`, `ensureNtp()`, `applyTimezone()`, `localHour()` |
| **`netprobe`** | 331 | HTTP végpont-tesztek, ICMP ping, DNS-kérdés | `testInternetHTTP()`, `lastHttpCode()`, `httpCodeLogParam()`, `testInternetPing()`, `buildDnsQuery()`, `dnsResponseOk()`, `testDnsServer()` |
| **`webportal`** | 596 | az AP beállító portál és az eszközönkénti AP jelszó | `startWebPortal()`, `stopWebPortal()`, `touchApDeadline()`, `announceApPassword()` |
| **`app_hooks.h`** | 22 | amit a főmodul ad a moduloknak | `feedWatchdog()`, `resetbutton()`, `wifiresetbutton()`, `waitWithButtons()`, `printUptime()`, `diagCounters()` |

> A `webportal` kapta a legszorosabb határt, mert ez a program
> **biztonságkritikus felülete**: ezen megy be a Wi-Fi jelszó. A POST-kezelő,
> a `/log` oldal, az űrlap, a `ConfigDraft` típus és maga a `server` objektum is
> a modulon belül van – a `ConfigDraft` a headerbe be sem került.
>
> Az `app_hooks.h` szándékosan rövid, és ez maga a mérőeszköz: **ha nőni kezd,
> az a rossz határhúzás első jele.** Egyszer már meg is történt – a
> `filesystemReady()` átmenetileg ide került, majd oda, ahová való (`configstore`).

### A főmodul (`bazsi_router_reboot.ino`, ~1700 sor, 61 függvény)

Ami maradt: a **döntéshozatal**. A modulok mérnek és tárolnak, a főmodul
eldönti, mi következik.

| Csoport | Függvények |
|---|---|
| Belépési pontok | `setup()` · `loop()` |
| A `loop()` ágai | `clearHealthyRunCounters()` · `handleFatalMode()` · `handleConfigMode()` · `runMonitorStateMachine()` |
| Az állapotgép | `runCheckState()` · `runResetState()` · `handleFirstStart()` |
| Wi-Fi | `initWiFi()` · `reconnectWifi()` · `wifiAuthFailed()` · `ensureFallbackDns()` |
| Wi-Fi-lépcső | `wifiFailed()` · `initWifiLadder()` · `wifiConnectedTick()` · `wifiResetPending()` · `wifiResetWaitMs()` · `wifiOutageMinutes()` · `wifiLadderPersist()` · `wifiLadderClear()` |
| Internet és router | `internetOk()` · `gatewayPingOk()` · `routerDnsCheck()` · `gatewayUnreachable()` · `resetWaitMs()` · `quietHours()` · `backoffMs()` |
| Relé | `startRouterReset()` · `reset_device()` · `holdRelayForSleep()` |
| A két alvás | `enterDeepSleep()` ← `apSleep()`, `fatalSleep()`; külön úton `handleStuckButton()` |
| Gombok | a két ISR (`onResetButtonEdge()`, `onWifiResetButtonEdge()`) · `armButtonLatches()` · `resetbutton()` · `wifiresetbutton()` · `pressedButtonNow()` · `doWifiReset()` |
| Watchdog és heap | `initWatchdog()` · `checkWatchdogResets()` · `feedWatchdog()` · `checkHeap()` · `initHeapState()` · `applyHeapCarry()` |
| Soros parancsok | `serialCommands()` · `runSerialCommand()` |
| Várakozások | `blockingDelay()` · `waitWithButtons()` · `onlineProbe()` · `onlineProbeDue()` |
| Leállás | `lockConfigBeforeShutdown()` · `restartFromButton()` · `enterFatal()` · `fatalHalt()` |
| Üzemmódváltás | `startConfigPortal()` – a naplómentés, a `deviceMode` és a LED-ek; a HTTP részt a `webportal` végzi |

### Amit érdemes tudni olvasás előtt

> **A fizikai sorrend és a logikai csoportosítás nem mindenhol esik egybe.** A
> főmodul elején álló előre-deklarációs blokk ezért van: feloldja a körkörös
> hivatkozásokat egy fordítási egységen belül.

Négy visszatérő minta szövi át az egészet:

| Minta | Hol számít |
|---|---|
| **Semmi dinamikus foglalás** | fix méretű pufferek mindenütt – a méret egyben validáció is |
| **Előjel nélküli különbség minden időzítésnél** | így a `millis()` 49,7 napos körbefordulása sehol nem számít |
| **„Csak a sorozat első tagja"** | `TEST FAIL` (kiesésenként), `WIFI LOST`, `STUCK BUTTON`, `LOW HEAP` – ez védi a 32 elemű naplót és a soros portot |
| **A zár atomikus megszerzése** | sosem „megnézem, szabad-e, aztán csinálom" – a webszerver taskja közben beléphet |
