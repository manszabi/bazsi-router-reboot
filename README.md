# 🔄 Bazsi Router Reboot

ESP32-C3 alapú automatikus router újraindító rendszer. Az eszköz folyamatosan figyeli az internetkapcsolatot, és ha kiesést észlel, relén keresztül automatikusan újraindítja a routert.

## 📋 Jellemzők

- **Automatikus internetkapcsolat-figyelés** – öt HTTP teszt, öt különböző üzemeltető felé
- **Automatikus router újraindítás** – relé segítségével áramtalanítja, majd visszakapcsolja a routert
- **Wi-Fi Manager** – böngészőből konfigurálható SSID, jelszó, IP-cím és gateway
- **Beépített beállító weboldal** – a programba fordítva, nincs feltöltendő fájlrendszer-tartalom
- **LittleFS** – a beállítások áramszünet után is megmaradnak
- **Deep Sleep védelem** – túl sok sikertelen próbálkozás után az ESP alvó módba lép (1 óra), majd újrapróbálkozik
- **Fizikai gombok** – reset és Wi-Fi reset gombok debounce-szal
- **Uptime kijelzés** – Soros porton folyamatosan látható az eszköz futási ideje

## 🔧 Hardver követelmények

| Komponens | Leírás |
|-----------|--------|
| **Mikrokontroller** | ESP32-C3 (pl. XIAO ESP32-C3) |
| **Relé modul** | 1 csatornás relé (router tápellátásának kapcsolására), a vezérlőbemenetén 22 kΩ lehúzó ellenállás a GND felé |
| **LED #1** | Állapot LED (D4 pin) |
| **LED #2** | Wi-Fi állapot LED (D3 pin) |
| **Nyomógomb #1** | Reset gomb (D1 pin) – ESP újraindítás |
| **Nyomógomb #2** | Wi-Fi reset gomb (D0 pin) – mentett Wi-Fi adatok törlése |

> ⚠️ **Strapping láb.** A `D0` = **GPIO2** az ESP32-C3 egyik strapping lába: a
> chip csak akkor bootol, ha a reset pillanatában GPIO2 = 1. A Wi-Fi reset
> gombot **bekapcsolás közben ne tartsd nyomva** – az eszköz el sem indulna,
> és ez ellen szoftver nem védhet. Új hardver revízióban a gombot érdemes
> szabad, nem-strapping lábra tenni (pl. `D2` = GPIO4, ami RTC-képes is).

### Pin kiosztás

| Pin | Funkció |
|-----|---------|
| `D0` | Wi-Fi reset gomb (INPUT_PULLUP) |
| `D1` | Reset / ébresztő gomb (INPUT_PULLUP) – RTC GPIO, deep sleepből is ébreszt |
| `D3` | Wi-Fi állapot LED |
| `D4` | Állapot LED |
| `D10` | Relé vezérlés (router tápellátás) – **külső 22 kΩ lehúzó a GND felé** |

### LED jelzések

| Státusz LED (`D4`) | Wi-Fi LED (`D3`) | Jelentés |
|---|---|---|
| be | be | Minden rendben, van Wi-Fi |
| be | ki | Nincs Wi-Fi kapcsolat (pl. a router bootolására várunk) |
| ki | ki | Alvás |
| be | **villog 1 Hz** | **AP beállító mód** – várja a böngészőt |
| **villog 2 Hz** | ki | **Router reset pulzus** – a router épp áram nélkül |
| **együtt villog 5 Hz** | **együtt villog 5 Hz** | **Végzetes hiba** (LittleFS / konfig / watchdog) |
| **felváltva villog 5 Hz** | **felváltva villog 5 Hz** | **Beragadt gomb** induláskor |

A négy villogó jelzés szándékosan elkülönül. Az AP mód és a router reset csak
az **egyik** LED-et villogtatja, más-más ütemben, és a másik LED állapota is
eltér; a két hibajelzés mindkettőt villogtatja, gyorsabban – az egyik együtt,
a másik ellenfázisban. A reset pulzus alatt azért villog a státusz LED, mert
90 másodpercnyi sötét LED ránézésre a halott eszköztől sem különböztethető meg.

> **A Wi-Fi LED késhet, legfeljebb egy percet.** A kapcsolat állapotát a
> tesztciklus ellenőrzi, a sikeres teszt utáni 60 mp-es várakozás alatt viszont
> nem. Ha a kapcsolat épp ekkor szakad meg, a LED legfeljebb **60 másodpercig**
> még világít, aztán a következő teszt elején elalszik (mérve: `LED4`). A
> működésre ez nem hat – csak a kijelzés késik.

## ⚙️ Működés

### 1. Első indulás – Wi-Fi konfiguráció

Ha nincs mentett Wi-Fi adat (vagy a Wi-Fi reset gombot megnyomtad):

1. Az ESP **Access Point módba** lép
2. Az AP neve: `ESP-<chipmodel>`, jelszó: `12345678`
3. Csatlakozz az AP-hez, majd nyisd meg a böngészőben: `192.168.4.1`
4. Töltsd ki az űrlapot (az **SSID, IP és Gateway előkitöltve** jelenik meg a
   mentett értékekkel – a **jelszó soha nem**, azt mindig újra kell írni):
   - **SSID** – a Wi-Fi hálózat neve (max. 32 karakter)
   - **Password** – a Wi-Fi jelszó (max. 63 karakter)
   - **IP Address** – az ESP kívánt statikus IP-je (opcionális, ha üres → DHCP)
   - **Gateway** – a router IP-je (opcionális, ha üres → DHCP)
5. Küldés után az ESP újraindul és csatlakozik a megadott hálózathoz

> **Az üres címmező törlést jelent.** Ez a DHCP-re való visszatérés útja – és
> pontosan ezért van a két címmező előkitöltve: enélkül aki csak a jelszót
> akarja átírni, a böngésző által üresen küldött mezőkkel csendben elveszítené
> a statikus IP-jét (mérve: `AP1`, `AP4`).
>
> **A jelszó mező viszont mindig üres**, mert azt nem küldjük ki a böngészőnek
> (`AP2`). Következmény: minden mentésnél újra be kell írni – üresen hagyva
> nyílt hálózatként mentődik, amit az űrlap ki is ír.
>
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
> nem használható, aminek szándékosan szóköz van a szélén. Az SSID-nél ez a
> tárolásból is következik – sima szövegként megy a fájlba, és beolvasáskor
> úgyis levágódna –, a jelszónál viszont tudatos döntés a másolás-beillesztéssel
> bekerülő szóközök ellen: a `v1:` + hexa alak a szóközt hibátlanul visszaadná.)
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
             mind az 5 teszt bukott
               (cycleIndex > 3)
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

| Kiírt sorszám | `cycleIndex` | Végpont | Üzemeltető | Elvárt válasz |
|:---:|:---:|---|---|---|
| 1 | 0 | `msftconnecttest.com/connecttest.txt` | Microsoft | `Microsoft Connect Test` |
| 2 | 1 | `cp.cloudflare.com/generate_204` | Cloudflare | **204 No Content** |
| 3 | 2 | `detectportal.firefox.com/success.txt` | Mozilla | `success` |
| 4 | 3 | `nmcheck.gnome.org/check_network_status.txt` | GNOME / NetworkManager | `NetworkManager is online` |
| 5 | 4 | `connectivitycheck.gstatic.com/generate_204` | Google | **204 No Content** |

> **Két számozás, szándékosan.** A soros port és a `/log` oldal **1-től**
> számol (`Teszt ciklus index = 1`…`5`), mert emberi olvasónak nincs „0-dik
> teszt". A `cycleIndex` változó viszont marad **0-alapú**: a `0`–`4`
> tartományhoz kötődik a végpontválasztó `if`-lánc és a `RESET_TRIGGER_CYCLE`
> küszöb is. A kódban tehát `0`–`4`, a kimeneten `1`–`5`.
>
> Öt végpont van (`MAX_CYCLE_INDEX + 1`), és az ötödik bukása után indul a
> router újraindítás. A `testInternetHTTP()` diszpécserében a Microsoft ága
> `else`-ként szerepel, tehát a `cycleIndex == 0`-t is az szolgálja ki.

**Nincs internet = mind az öt teszt elbukik.** Öt különböző végpont, öt
független üzemeltető, két ellenőrzési mód. Bármelyik siker nullázza a
számlálókat, tehát egyetlen szolgáltató kiesése soha nem látszik
internetkimaradásnak. A küszöb szándékosan szigorúbb az iparági szokásnál
(a Nagios `max_check_attempts` alapértelmezése 3 egymást követő bukás), mert
egy téves reset ára ~11,5 perc kiesés, a plusz szigorúságé viszont csak
~1 perc késleltetés.

> **Ha DNS-szűrőt futtatsz a hálózaton** (Pi-hole, AdGuard Home), ellenőrizd a
> blokklistáidat. A kapcsolat-ellenőrző domainek – `connectivitycheck.gstatic.com`,
> `detectportal.firefox.com` – tipikus célpontjai a telemetria-ellenes listáknak,
> épp azért, hogy az OS/böngésző ne „telefonáljon haza". A legelterjedtebb lista,
> a Pi-hole alapértelmezett StevenBlack *unified* (79 746 domain), ellenőrizve
> **egyiket sem** tiltja (csak a `csi.gstatic.com`-ot), és a
> *fakenews-gambling-porn-social* változata sem – de a szűkebb, vendor-specifikus
> listák eltérnek, ezért a sajátodat nézd meg. Egy blokkolt név `0.0.0.0`-ra
> oldódik vagy NXDOMAIN-t ad, tehát az adott teszt **mindig elbukik**. Ez
> önmagában nem okoz téves resetet – a **3.** tesztre (Mozilla) csak akkor
> kerül sor, ha az 1. és a 2. is elbukott –, de elveszíted a redundancia egy
> részét. A soros naplóban a `Teszt ciklus index = N` sor és a `/log` oldal
> `TEST FAIL` bejegyzésének paramétere is megmondja, melyik végpont bukott el
> (mindkettő 1-alapú, a fenti táblázat szerint).

> **A soros kimenet két száma, és hogy miért ott áll, ahol.**
> A `Teszt ciklus index = N` a teszt **előtt** áll, mert azt mondja meg, melyik
> végpont következik (`1`…`5`). A `Test failed. | Hibák száma = N / 5` viszont a
> teszt **után**, mert az már az eredmény – hány egymás utáni bukás van, és hány
> után jön a router újraindítása. (Korábban a hibaszámláló is a teszt előtt
> állt, így a sorozat első tesztjénél mindig `0`-t mutatott.)
>
> **Sikeres teszt csak a `Successful Test` sort adja.** A `testInternetHTTP()`
> siker esetén hallgat: sem a kapott törzset, sem külön nyugtázó sort nem ír ki.
> Eltéréskor viszont kiírja a **kapott törzset** és a `Hamis érték!` sort – ott
> ugyanis épp az a kérdés, mi jött vissza a várt válasz helyett (captive portál,
> megváltozott végpont).

#### Miért nincs ping az internettesztek között

Az ICMP nem bizonyít sem névfeloldást, sem TCP-t. A leggyakoribb valós hiba
– amikor az olcsó router DNS-továbbítója befagy – mellett a ping tökéletesen
megy, miközben a házban egyetlen eszköz sem éri el az internetet. Mérve: a
korábbi, pinget is tartalmazó változat **egy órán át 41 bukott HTTP teszt
mellett nulla** router resetet indított, mert az 1-es indexen a `1.1.1.1`
ping mindig sikerült és nullázta a számlálókat. Nem véletlen, hogy egyetlen
nagy implementáció sem ICMP-vel validál: a NetworkManager (libcurl HTTP), a
Firefox (`detectportal`), a Windows NCSI (DNS + HTTP) mind HTTP-t használ.

A ping két helyen maradt meg, és **egyik sem szavazhat** az internettesztben:

1. **A saját gateway ellenőrzése** (`gatewayUnreachable()`), ahol pont az a
   kérdés, hogy a 3. rétegben elérünk-e egyáltalán valamit.
2. **A hosszú várakozások korai lezárása** (`onlineProbe()`, lásd a
   „Ha nem sikerül csatlakozni a Wi-Fihez" szakaszt). Itt a ping legrosszabb
   esetben annyit ér el, hogy a várakozás korábban ér véget – utána **azonnal
   a rendes HTTP tesztsorozat következik**, ami a befagyott DNS-t így is
   elbukja. A fenti hibát tehát nem hozza vissza: a `H9` teszt épp azt méri,
   hogy sikeres ping mellett is lefut a router újraindítás.

Az internetteszt maga továbbra is **kizárólag HTTP** – ezt a `H8` teszt őrzi.

#### A két ellenőrzési mód

A **204-es ellenőrzés** szigorúbb, mint a szöveg-egyeztetés: egy captive
portál nem tud `204 No Content`-et adni, mert neki éppenséggel tartalmat kell
küldenie (bejelentkező oldal vagy átirányítás). A `testInternetHTTP()` akkor
vált erre az ágra, ha az elvárt válasz üres sztring. Ugyanezt a döntést hozza
a NetworkManager is (`src/core/nm-connectivity.c`: 204 → „no content, as
expected", bármi más → portál).

> A szöveges ellenőrzés a válaszból legfeljebb 96 bájtot olvas be, és levágja
> a záró sortörést az összehasonlítás előtt.

#### Miért bontja a firmware maga a chunked keretezést

A `HTTPClient` a `Transfer-Encoding: chunked` darabhatárait **csak** a
`getString()` és a `writeToStream()` útján bontja le (`HTTPClient.cpp`:
`_transferEncoding == HTTPC_TE_CHUNKED`). Egyiket sem használjuk: mindkettő
korlátlanul foglal vagy ír, egy captive portál többszáz kilobájtos válaszán
pedig épp ezt akarjuk elkerülni – ezért olvassuk a `begin()`-nek átadott
`WiFiClient`-et közvetlenül, fix méretű pufferbe.

Csakhogy a nyers streamben a keretbájtok is ott vannak. Egy chunked `success`
válasz így néz ki a dróton:

```
7\r\nsuccess\r\n0\r\n\r\n
```

A `strcmp()` ezt nem látná `success`-nek, tehát a **tökéletesen működő végpont
is bukott tesztnek számítana**. Öt ilyen egymás után = fölösleges router
újraindítás. A `readChunked()` ezért lebontja a keretezést:

- hexa méret sor, opcionális `;kiterjesztés`-sel, kis- és nagybetűvel egyaránt;
- a 96 bájtos puffer határán megáll – nem olvas végig egy darabokban érkező
  captive portált sem;
- túlcsorduló méretnél és nem hexa méret sornál feladja, és a teszt elbukik.
  Nem találgatunk: egy szabálytalanul keretező köztes doboz **soha** ne
  számítson sikeres internettesztnek.

A ma használt öt végpont mind `Content-Length`-et küld, tehát ez a kód
gyakorlatban nem fut le – de egy közbeiktatott proxy bármikor átkeretezheti a
választ, és ez a hiba némán, a redundancia elvesztésével jelentkezne. A
`CH1`–`CH5` forgatókönyvek fedik.

#### Router reset folyamat

1. Ha **mind az 5 teszt elbukott** (`cycleIndex > 3`, azaz az 5. végpont is):
   - Relé **bekapcsol** → router áramtalanítva
   - **90 másodperc** várakozás (RESET_PULSE)
   - Relé **kikapcsol** → router visszakap áramot
   - **legfeljebb 10 perc** várakozás (RESET_DELAY) – idő a router bootolásához;
     60 mp-enként megnézzük, hogy visszajött-e a hálózat és az internet, és ha
     igen, itt azonnal továbblépünk
   - Wi-Fi újracsatlakozás (a korai kilépés ágán ez is elmarad – a kapcsolatot
     épp az imént igazoltuk)

   > **Egyetlen feltétel dönt: `cycleIndex > 3`.** Nincs külön „N hiba" küszöb.
   > Mivel a két számláló együtt lép (`failedCount == cycleIndex + 1`), ez
   > pontosan **5 egymás utáni bukást** jelent – de a feltétel szándékosan a
   > *végpont-lefedettséget* mondja ki, nem a darabszámot: mind az öt
   > üzemeltetőt végig kell próbálni, hogy egyetlen szolgáltató kiesése soha ne
   > látszódjon internetkimaradásnak.
   >
   > **Bármelyik sikeres teszt nullázza a folyamatot** (`cycleIndex`,
   > `failedCount` és a `resetEvents` is), tehát a sorozatnak megszakítás
   > nélkülinek kell lennie.
   >
   > Egy kivétel van: ha a **Wi-Fi maga nem jön vissza**, a firmware azonnal
   > `cycleIndex = 4`-re ugrik, és nem játssza végig az öt tesztet – nincs
   > hálózat, amin bármelyik végpont elérhető lenne.

2. Ha **4 router újraindítás** sem hozza vissza az internetet → deep sleep 1 órára,
   majd magától ébred és újrapróbálja. (A `maxfailureEvents = 5` a *reset esemény*
   számláló határa; a számláló még a reset előtt nő, ezért 4 tényleges újraindítás történik.)

### 3. Fizikai gombok

| Gomb | Funkció |
|------|---------|
| **Reset** (D1) | ESP32-C3 azonnali újraindítása; deep sleepből felébreszti az eszközt |
| **Wi-Fi Reset** (D0) | Mentett Wi-Fi adatok törlése + ESP újraindítás → visszaáll AP módba |

> **Egy rövid koppintás is elég.** A firmware minden saját várakozó ciklusában
> 10 ms-onként mintavételezi mindkét gombot (mérve: `BTN1`) – egy futó **HTTP
> teszt alatt viszont nem tud**, mert a `http.GET()` az ESP32 core blokkoló
> hívása. Ez az ablak legfeljebb egy kérésnyi: 15 mp hallgató szervernél,
> 33 mp halott DNS-nél (mérve: `BTN2`).
>
> Ezt az ablakot **megszakítás-alapú retesz** hidalja át. Mindkét gomb lábán
> `CHANGE` megszakítás fut, és a kezelő **megméri a lenyomás hosszát**: a
> lefutó élen jegyzi az időt, a felfutón pedig csak akkor reteszel, ha a gomb
> legalább 50 ms-ig lent volt. A debounce tehát ugyanaz maradt, csak
> hardveresen történik – és akkor is működik, amikor a `loop()` épp nem tud
> mintavételezni. A blokkoló szakasz alatti gombnyomás így **nem vész el**,
> csak a szakasz végéig késik (mérve: `LAT1`).
>
> A retesz nem kerül meg semmit: egy zajtüske továbbra sem indít újra (`LAT2`),
> fájlírás közben a nyomás megmarad, de nem hat (`LAT4`), és egy beragadt gomb
> nem reteszel, mert felfutó éle sosem lesz (`LAT5`).

> **Pattogás (kontaktus-visszaugrás).** Egy mechanikus gomb sem a lenyomáskor,
> sem a **felengedéskor** nem ad tiszta élt. A pollozott ág elölről indítja a
> debounce-t minden `HIGH` olvasásra; a megszakításos retesz a felfutó élen
> **nullázza is** a lenyomás idejét, így a felengedési pattogás után nem marad
> hátra „félig lenyomott" állapot. Ez utóbbi azért lényeges, mert egy elavult
> időbélyeggel egy jóval későbbi, magában ártalmatlan felfutó él azt látná,
> hogy a gomb „régóta" nyomva van, és spontán újraindítást okozna (mérve:
> `BNC1`). Egy folyamatosan recsegő, **kopott** gomb szándékosan **nem** indít
> újra és nem is reteszel (`BNC3`) – inkább ne reagáljon, mint hogy magától
> újrainduljon.

> ⚠️ Induláskor **mindkét** gombot ellenőrizzük. Ha bármelyik nyomva van, előbb
> **3 másodpercet várunk az elengedésére** – a reset gomb LOW szintre ébreszt,
> tehát a felhasználó szükségképpen még nyomva tartja, amikor az eszköz bootolni
> kezd; egy pillanatnyi beolvasás a saját ébresztő gombnyomását nézné
> beragadásnak. Ha 3 másodperc után is nyomva van, a két LED 3 másodpercig
> **felváltva** villog (megkülönböztetésül a végzetes hibától, ahol együtt
> villognak), majd az ESP 60 másodpercre deep sleep módba lép. Ilyenkor a gombos
> ébresztés **nincs** bekapcsolva, különben a beragadt gomb végtelen boot loopot
> okozna.

### Deep sleep és ébredés

| | |
|---|---|
| **Elalvás oka** | 5 sikertelen router reset, vagy sikertelen Wi-Fi újracsatlakozás |
| **Alvás hossza** | 1 óra (timer), vagy 60 mp beragadt gomb esetén |
| **Ébresztés** | timer **vagy** a reset gomb (D1) lenyomása. Végzetes hiba utáni alvásnál **csak a gomb** |
| **Ébredés után** | teljes újraindulás – a `setup()` fut le elölről |

Az ESP32-C3 deep sleepből mindig **újraindulással** ébred: a RAM tartalma elvész,
így a hibaszámlálók (`resetEvents`, `failedCount`) nullázódnak, és az eszköz
tiszta lappal kezdi az internet tesztelését. Három érték él RTC memóriában:
az újrapróbálkozási körök száma (`rtcRetryRounds`, csak a deep sleepet éli túl),
a watchdog-újraindulások számlálója és a 32 bejegyzéses diagnosztikai napló
(mindkettő `RTC_NOINIT`, a resetet is túléli – csak az áramtalanítás törli).

> ⚠️ **Hardver: a relé lába és a lehúzó ellenállás.** A relé vezérlése a
> `D10` = **GPIO10** lábon van, **22 kΩ lehúzó ellenállással a GND felé**.
> (A korábbi `D5` = GPIO7 bekötés nem bizonyult stabilnak. A `D10` nem
> strapping láb – a C3-on `GPIO2`, `GPIO8`, `GPIO9` az –, tehát a reset alatti
> szintje a bootot nem befolyásolja.)
>
> Deep sleep alatt az ESP32-C3 digitális lábai (GPIO6–21, köztük a GPIO10)
> alapból nagyimpedanciás állapotba kerülnek. A firmware ezért minden elalvás
> előtt **rögzíti a relé lábát** LOW-ra a `gpio_hold_en()` +
> `gpio_deep_sleep_hold_en()` párossal, és ébredés után – miután a lábat már
> maga hajtja – oldja fel, így a relé alvás alatt sem lebeg.
> A 22 kΩ lehúzó ettől függetlenül **nem elhagyható**: a bekapcsolás és a
> program indulása közti ablakban a hold még nem él, és a szoftveres védelem
> esetleges hibája ellen is ez az utolsó háló.
> (Forrás: ESP-IDF `driver/gpio.h` – a `gpio_hold_en` C3-ra vonatkozó
> megjegyzése és a `gpio_deep_sleep_hold_en` leírása.)

## 📁 Projekt struktúra

```
bazsi-router-reboot/
├── bazsi_router_reboot.ino   # Fő sketch: a döntéshozatal (setup, loop, állapotgép)
│
├── limits_config.h           # a konfig mezők méretkorlátai (se állapot, se függvény)
├── app_hooks.h               # amit a főmodul ad a moduloknak (szándékosan rövid)
├── strutil.h/.cpp            # tiszta szöveg-segédek
├── secret.h/.cpp             # a mentett jelszó összekeverése
├── sync.h/.cpp               # a két task közti osztott állapot
├── configstore.h/.cpp        # a négy mentett érték + LittleFS
├── eventlog.h/.cpp           # diagnosztikai napló + valós idő (NTP)
├── netprobe.h/.cpp           # HTTP végpont-tesztek és ICMP ping
├── webportal.h/.cpp          # az AP beállító portál HTTP felülete
│
├── partitions_custom.csv     # Egyedi partíciós tábla: OTA nélkül, 512 KiB LittleFS
├── test/                     # host tesztkészlet (295 forgatókönyv)
└── LICENSE                   # MIT License
```

A Wi-Fi beállító weboldal a programba van fordítva (`FORM_HEAD` / `FORM_TAIL`
konstansok + a `sendConfigForm()` generálja a mezőket), a flash `.rodata`
szekciójában – nincs külön feltöltendő `data/` mappa. A LittleFS-re így csak a
négy konfigurációs fájl és a mentett napló kerül (`/ssid.txt`, `/pass.txt`,
`/ip.txt`, `/gateway.txt`, `/evlog.bin` – ez utóbbi ~400 bájt).

### A program felépítése

**Nyolc fordítási egység**, ~4700 sor, **98 függvény**, 44%-a komment. A gerinc
három üzemmód (`MODE_MONITOR` / `MODE_CONFIG` / `MODE_FATAL`) és három állapot
(`TESTING` → `SUCCESS`, vagy `TESTING` → `FAILURE`). `MODE_MONITOR`-ból van út
a másik kettőbe, **visszaút nincs**.

A modulok külön fordítási egységek, saját headerrel: **amit a header nem hirdet
meg, azt a linker sem találja meg**. Fentről lefelé, minden modul csak a nála
lentebb állóktól függ:

| Modul | Mit birtokol | Mit exportál |
|---|---|---|
| `webportal` | az AP portál HTTP felülete – ez a **biztonságkritikus** felület | **3 függvény** (a POST-kezelő, a `/log` oldal, az űrlap és a `server` mind belső) |
| `netprobe` | HTTP és ICMP mérés – csak **mér**, nem dönt | `testInternetHTTP()`, `testInternetPing()` |
| `eventlog` | a diagnosztikai napló és a valós idő | `logEvent()`, `saveEventLog()`, `ensureNtp()`, … |
| `configstore` | a négy mentett érték, a fájljaik, a LittleFS | a négy puffer + az olvasó/író függvények |
| `sync` | a két task közti osztott állapot | 7 függvény – a jelzők maguk **belsők** |
| `secret` | a jelszó összekeverése | `encodeSecret()`, `decodeSecretInPlace()` |
| `strutil` | `trimInPlace()` | 1 függvény |

A főmodulban a **döntéshozatal** maradt: `setup()`, `loop()`, az állapotgép
(`runTestingState()` / `runFailureState()` / `runSuccessState()`), a Wi-Fi, a
relé, az alvások, a gombok, a watchdog és a heap-felügyelet.

Teljes függvénylistával és a határhúzás indoklásával:
[MUKODES.md 16. fejezet](MUKODES.md).

## 📦 Szükséges könyvtárak

| Könyvtár | Leírás |
|----------|--------|
| `WiFi.h` | ESP32 Wi-Fi kezelés (a board package része) |
| `ESPAsyncWebServer` (**ESP32Async** fork, tesztelve: 3.12.0) | Aszinkron webszerver |
| `AsyncTCP` (**ESP32Async** fork, tesztelve: 3.5.0) | Aszinkron TCP az ESPAsyncWebServer-hez |
| `LittleFS` | Fájlrendszer a flash memóriában (a board package része) |
| `HTTPClient` | HTTP kérések az internettesztekhez (a board package része) |
| `ESPping` (dvarrel, tesztelve: 1.0.5) | ICMP ping a saját gateway ellenőrzéséhez |

> A webszervernél kifejezetten a karbantartott **ESP32Async** forkot használd
> (`ESP32Async/ESPAsyncWebServer` + `ESP32Async/AsyncTCP`): a régi, elhagyott
> me-no-dev változat API-ja több ponton eltér (pl. `params()` visszatérési
> típusa, `getParam()` const-ossága, a hiányzó fájl hibakódja).

## 🚀 Telepítés

### Arduino IDE

1. Telepítsd az **ESP32 board support**-ot az Arduino IDE-ben
2. Telepítsd a szükséges könyvtárakat (Library Manager vagy kézi telepítés)
3. Válaszd ki a board-ot: **ESP32-C3** (pl. *XIAO_ESP32C3*)
4. Nyisd meg a `bazsi_router_reboot.ino` fájlt. A mellette lévő `.h` és `.cpp`
   modulokat **ugyanabban a mappában** kell hagyni – az Arduino a sketch mappa
   minden forrását lefordítja és hozzálinkeli (a szerkesztőben külön fülként
   jelennek meg).

   > **A mappanévnek egyeznie kell a `.ino` nevével.** A klónozott repó mappája
   > `bazsi-router-reboot` (kötőjellel), a sketch viszont
   > `bazsi_router_reboot.ino` (alulvonással) – ezt az Arduino nem fogadja el
   > (`main file missing from sketch`). Másold a `.ino`-t, az összes `.h`/`.cpp`
   > modult és a partíciós táblát egy `bazsi_router_reboot` nevű mappába:
   >
   > ```bash
   > mkdir -p bazsi_router_reboot
   > cp bazsi_router_reboot.ino *.h *.cpp bazsi_router_reboot/
   > cp partitions_custom.csv bazsi_router_reboot/partitions.csv
   > ```
   >
   > (Az Arduino IDE 2.x fel is ajánlja a másolást, amikor megnyitod a fájlt.)
   > A CI ugyanezt teszi a fordítás előtt.
5. Másold a `partitions_custom.csv`-t a sketch mappájába **`partitions.csv`**
   néven – a core a sketch melletti `partitions.csv`-t automatikusan használja.
   (Ha inkább gyári sémát választanál a `Tools` → `Partition Scheme` menüből,
   olyat válassz, amiben van SPIFFS/LittleFS partíció.)
6. Töltsd fel a sketch-et az ESP-re

Fájlrendszert **nem kell feltölteni**: a weboldal a programban van, a LittleFS-t
az első indulás formázza meg magától.

### PlatformIO

```ini
[env:esp32c3]
platform = espressif32
board = seeed_xiao_esp32c3
framework = arduino
lib_deps =
    ESP32Async/ESPAsyncWebServer @ ^3.12.0
    ESP32Async/AsyncTCP @ ^3.5.0
    dvarrel/ESPping @ ^1.0.5
board_build.filesystem = littlefs
board_build.partitions = partitions_custom.csv
```

## 🗂️ Partíciós tábla (`partitions_custom.csv`)

A gyári Arduino sémák vagy két app-partíciót tartanak fenn az OTA-nak
(`default.csv`: 2 × 1,25 MB), vagy ~1,5 MB fájlrendszert adnak. Ez a program
egyiket sem használja: **OTA nincs benne** (USB-ről flashelünk), a LittleFS-en
pedig összesen négy rövid szöveges fájl él. A weboldal a programban van, tehát
a fájlrendszernek nem kell weblapot tárolnia.

| Név | Típus | Cím | Méret | Mire |
|---|---|---|---|---|
| `nvs` | data / nvs | `0x9000` | 20 KiB | a core saját kulcs-érték tára |
| `phy_init` | data / phy | `0xE000` | 4 KiB | rádió kalibrációs adatok |
| `factory` | app / factory | `0x10000` | **3520 KiB (~3,44 MB)** | a program |
| `spiffs` | data / spiffs | `0x380000` | **512 KiB** | LittleFS (`/ssid.txt`, `/pass.txt`, `/ip.txt`, `/gateway.txt`, `/evlog.bin`) |

A tábla pontosan kitölti a XIAO ESP32-C3 4 MB-os flash-ét. Az app partíció
kezdőcíme 64 KB-ra, a data partíciók 4 KB-ra igazítottak (a `0xF000`–`0x10000`
közti 4 KB emiatt marad ki).

Amit ez hoz:

- a program **~3,44 MB**-ot kaphat 1,25 MB helyett,
- az első indulás LittleFS-formázása 15–20 mp helyett **4–7 mp** (128 szektor
  × 30–50 ms), ami a watchdog élesedése előtti szakaszt is lerövidíti,
- cserébe **OTA frissítés nincs** – a program csak USB-n keresztül flashelhető.

> A LittleFS partíció címkéje szándékosan `spiffs`: az Arduino core a
> `LittleFS.begin()`-ben ezt a címkét keresi. A címke a formátumot nem
> határozza meg, a partíció tartalma LittleFS.

## ⏱️ Időzítések

| Paraméter | Érték | Leírás |
|-----------|-------|--------|
| `interval` | 20s | Wi-Fi csatlakozási timeout |
| `SUCCESS_DELAY` | 1 perc | Várakozás sikeres teszt után |
| `PROBE_DELAY` | 12s | Várakozás sikertelen teszt után (újrapróbálkozás előtt) |
| `RESET_PULSE` | 90s | Router áramtalanítás időtartama |
| `RESET_DELAY` | **max.** 10 perc | Várakozás a router reset után (bootolási idő) – korábban véget ér, ha a hálózat és az internet is visszajött |
| `firstStartDelay` | **max.** 10 perc | Első indítás utáni várakozás – ugyanígy korábban is véget érhet |
| `ONLINE_PROBE_INTERVAL_MS` | 60 mp | Ilyen sűrűn nézzük a két hosszú várakozás alatt, hogy vége lehet-e |
| `maxfailureEvents` | 5 | A reset esemény számláló határa → **4** tényleges router újraindítás után deep sleep |
| `wifi_maxRetries` | 3 | Wi-Fi újracsatlakozási próbálkozások |
| `wifiInterval` | 30s | Szünet a próbálkozások között |
| `MAX_RETRY_ROUNDS` | 33 | 33 × 25,5 perc + 32 × 60 perc = **46 óra** türelem, körönként egy router újraindítással (az utolsó kör már nem alszik) |
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
Ping teszt futtatása (internet - 1.1.1.1)...
Ping 1 sikeres.
Ping 2 sikeres.
✅ Ping teszt sikeres.
Uptime: 0h 0m 2s
First start wait end (halozat es internet visszajott).
Uptime: 0h 0m 2s
Beginning Test.
Teszt ciklus index = 1
Uptime: 0h 3m 1s
Successful Test

SUCCESS_DELAY delay start.
```

### Indulás és lezárás

- **Indulás:** `Serial.begin(115200)`, majd max. **3 mp** várakozás a gazdára és
  **0,5 mp** CDC-beállás – csak ezután megy ki az első sor, hogy egy frissen
  csatlakoztatott monitoron ne vesszenek el az induló üzenetek.
- **Alvás előtt:** `flush()`, majd `end()` – **a legvégén**, minden más
  leállítási lépés után. A lezárás után egyetlen sort sem írunk.
- **Újraindítás előtt:** `flush()` közvetlenül az `ESP.restart()` előtt.

### Mennyit ír?

A kimenet nem áraszthatja el a konzolt: a költségvetés **30 sor/perc**, és ez a
legellenségesebb helyzetekben is tartja magát – pislákoló Wi-Fi (9–12), a küszöb
körül ingadozó heap (15), mindig bukó naplómentés tartós kiesés alatt (10) –,
míg normál működésben 9. Az AP beállító mód és a végzetes hiba villogó ciklusa a
ritkított heap-soron kívül **egyetlen sort sem ír**. Ezt az
ismétlődő események szűrése tartja fenn (`TEST FAIL`, `WIFI LOST`,
`STUCK BUTTON` sorozatokból csak az első).

## 🧠 Heap felügyelet

A sketch **saját kódja** nem allokál dinamikusan – ezt a `make lint` ki is
kényszeríti. (Néhány *könyvtári* hívás azonban `String`-et ad vissza érték
szerint, és az heapről dolgozik: `http.begin()`, `http.header()`, `WiFi.SSID()`.
Rövid életű foglalások, de foglalások.) A heapet érdemben az
ESPAsyncWebServer, az AsyncTCP, a HTTPClient és a Wi-Fi driver használja.
Egy lassú szivárgás hónapokig észrevétlen
marad, aztán az eszköz „csak úgy" nem működik – **allokációs hibánál a legtöbb
könyvtár csendben elbukik, nem panikol, tehát a watchdog sem fogja meg.**

| | |
|---|---|
| Mintavétel | 10 mp-enként |
| Soros állapotsor | 30 percenként (szabad heap, legnagyobb összefüggő tömb, **valaha volt legkisebb**) |
| Figyelmeztetés | 25 000 B alatt – csak a küszöb **átlépésekor**, nem minden mérésnél |
| Önkéntes újraindulás | 12 000 B alatt, vagy ha a legnagyobb tömb 6 000 B alá esik – de csak **3 egymást követő** mérés után |
| Felső korlát | 3 heap-újraindulás, utána `MODE_FATAL` (a boot loop rosszabb, mint a megállás) |

```
Uptime: 0d 0h 0m 10s
Heap: szabad 178432 B, legnagyobb tomb 110000 B, valaha volt legkisebb 178432 B
```

**Nem indul újra**, ha AP beállító módban vagy (elveszne a beírt konfiguráció),
végzetes hiba módban, fájlírás közben, a relé impulzusa alatt, vagy **a router
reset utáni ellenőrző ablakban**. Mindegyik helyzetnek megvan a maga kiútja: a
portál 5 perc után elalszik, a többi eset pedig a következő mérésnél újra sorra
kerül.

> Az utolsó kizárás a legkevésbé nyilvánvaló. A gateway-eszkaláció **kétfázisú**:
> ha a saját gateway sem érhető el, a router kap egy esélyt (újraindítás), és a
> várakozás után **újra** ellenőrizzük – ha még mindig nincs gateway, a statikus
> IP a rossz, jön az AP mód, hogy javítani lehessen. Azt, hogy „az első fázis már
> lefutott", három sima globális hordozza együtt, és egy újraindulás mindhármat
> elveszti. Az eszköz elölről kezdené: a router kapna **még egy fölösleges
> áramtalanítást**, és az AP módba menetel egy teljes körrel később születne meg –
> épp az járna rosszul, akinek a statikus IP-jét javítani kellene.

**Amit az újraindulás átvisz** – két számláló, mindkettő egy-egy többfázisú
létra állását őrzi:

- a `testState.resetEvents`: hányszor indítottuk már újra a routert. Ez viszi az
  eszközt az ötödiknél az 1 órás alvásba; enélkül **végtelenül újraindíthatná a
  routert** ahelyett, hogy elalszik.
- az `rtcRetryRounds`: a **2 napos ablak** állása. Ez `RTC_DATA_ATTR`, tehát
  szoftveres resetre szándékosan nullázódik (a *felhasználói* beavatkozás tiszta
  lapot kap) – a heap-újraindulás viszont nem felhasználói beavatkozás. Átvitel
  nélkül az eszköz sosem érné el a 2 napos határt, vagyis **sosem menne AP
  módba**, és a felhasználó sosem kapna esélyt a javításra.

Mindkettő az RTC memórián megy át, és a `setup()` pontosan egyszer használja fel.
Gombnyomásnál nincs átvitel, tehát ott marad a tiszta lap. Részletek:
[MUKODES.md](MUKODES.md)

## ✅ Tesztelt konfiguráció

| | |
|---|---|
| **Board package** | ESP32 Arduino **3.3.11** |
| **Beágyazott ESP-IDF** | `release_v5.5` (`esp32-arduino-libs-idf-release_v5.5-b774170f`) |
| **Board** | XIAO ESP32-C3 (`D0`=GPIO2, `D1`=GPIO3, `D3`=GPIO5, `D4`=GPIO6, `D10`=GPIO10) |
| **ESPAsyncWebServer** | ESP32Async fork, **3.12.0** |
| **AsyncTCP** | ESP32Async fork, **3.5.0** |
| **ESPping** | dvarrel, **1.0.5** |
| **Partíciós séma** | `partitions_custom.csv` (OTA nélkül, 512 KiB LittleFS) |

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

A watchdog **a soros port beállása után azonnal** élesedik, még a
gombellenőrzés és a LittleFS csatolása előtt – vagyis a `setup()` gyakorlatilag
teljes egészében felügyelet alatt van:

| Szakasz | Felügyelve? | Megjegyzés |
|---|:---:|---|
| `pinMode`-ok, relé hold feloldása, `Serial.begin()` + max. 3 mp várakozás | nem | ide még nem megy ki soros üzenet, tehát az `initWatchdog()` hibajelzése is elveszne |
| gombellenőrzés, `checkWatchdogResets()` | **igen** | 3 mp türelem az elengedésre, aztán 3 mp villogás és deep sleep |
| LittleFS csatolás / **formázás** | **igen** | lásd lent |
| konfiguráció beolvasása | **igen** | négy rövid fájl |
| `WiFi.persistent()`, `initWiFi()`, AP portál indítása | **igen** | a Wi-Fi init a legvalószínűbb lefagyási pont; a 20 mp-es várakozás magától etet |
| `loop()` | **igen** | a core minden `loop()` körben etet (`loopTaskWDTEnabled`) |

> **Miért fér bele a formázás?** A `LittleFS.begin(true)` első indításkor
> formáz, etetés nélkül. A `partitions_custom.csv` 512 KiB-os partíciója
> **128 szektor**: tipikusan 30–50 ms/szektor, azaz **4–7 mp**, és még a
> szélsőségesen lassú, 400 ms/szektoros esetben is **~51 mp** – mindkettő a
> 90 mp-es `WDT_TIMEOUT_MS` alatt marad. A korábbi, ~1,5 MB-os sémánál ez
> 15–20 mp volt tipikusan, de a rossz esetben **153 mp**, azaz a timeout
> fölött – **ezért volt ott indokolt kihagyni a formázást a felügyeletből, és
> ezért fér bele most.** A kisebb partíció tette lehetővé ezt a változtatást.

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
| Van SSID, de nem érhető el | **legfeljebb 10 perc várakozás** (`firstStartDelay`), majd 3 próba → ha így sem megy: **AP portál** |

A 10 perces várakozás a lényeg: áramszünet után az ESP másodpercek alatt
elindul, a router viszont percekig bootol.

**De nem várunk feleslegesen.** A várakozás alatt 60 mp-enként lefut egy
kétlépcsős próba, és a **sorrend a lényeg**:

1. **Wi-Fi – csatlakozási kísérlet, nem teszt.** Ha nincs kapcsolat, a firmware
   csak *kezdeményez* egyet (aszinkron `WiFi.begin()`, nem vár rá), és kilép.
   A kudarcnak **nincs következménye**: nem nő számláló, nem változik állapot,
   a várakozás megy tovább. Ehhez semmi köze a „3 próba, aztán router reset"
   eszkalációnak – az csak a `firstStartDelay` *lejárta után* kezdődik.
2. **Internet – csak ha az 1. lépés szerint van kapcsolat.** Ilyenkor megy ki
   egy ping a `1.1.1.1`-re. Hálózat nélkül **el sem indul**: pingelni
   értelmetlen (és pazarlás) lenne, ha a csomag ki sem tudna menni.

A várakozás akkor ér véget korábban, ha **mindkét lépés** sikerül; ekkor jöhet
a rendes tesztsorozat.

**A próba ismétlődik, tehát nem kell induláskor meglennie mindennek.** Ha egy
adott próba elbukik, a várakozás egyszerűen megy tovább a következő ütemig – és
ha a kapcsolat a 10 perc *bármely* pontján helyreáll, a kilépés az azt követő
ütemben megtörténik. A teljes időt tehát csak akkor várjuk ki, ha végig nincs
meg a hálózat vagy az internet. (Mérve: `OP7` – a 3,5 percnél visszatérő
internetnél a várakozás 4 perc alatt lezárult, nem 10 alatt.)

Ugyanez a próba fut a router reset utáni `RESET_DELAY` alatt is – ott ráadásul
az újracsatlakozási kört is megspórolja, hiszen a kapcsolatot épp az imént
mértük.

> **Miért ping, amikor az internettesztek szándékosan HTTP-t használnak?**
> Mert itt más a tét. A ciklikus tesztnél egy hamis pozitív végzetes: befagyott
> router-DNS mellett a ping megy, és az eszköz sosem indítaná újra a routert.
> Itt viszont a ping legrosszabb esetben annyit ér el, hogy a várakozás
> korábban ér véget – utána **azonnal a rendes HTTP tesztsorozat következik**,
> ami a befagyott DNS-t így is elbukja. A hamis pozitív itt tehát nem elrejt
> egy hibát, hanem hamarabb deríti ki. Cserébe a ping olcsó: nincs névfeloldás,
> nincs TCP, kevés rádióidő.

> **Flash kímélés.** A próba semmit nem ír a fájlrendszerre, és a benne lévő
> `WiFi.begin()` sem ír NVS-be, mert a `setup()` `WiFi.persistent(false)`-t
> hívott. A 60 mp-es köz így is korlátoz: egy 10 perces várakozás alatt
> legfeljebb 10 próba fut.

### Működés közben megszakad a kapcsolat

```
kapcsolatvesztés → 3 próba (30 mp szünetekkel)
                 → sikertelen → ROUTER ÚJRAINDÍTÁS (relé ki 90 mp, be)
                 → max. 10 perc várakozás (RESET_DELAY, korán is végetérhet)
                 → 3 próba (30 mp szünetekkel)
                 → sikertelen → AP beállító portál
```

> **Ha te magad áramtalanítod a routert:** az eszköz ezt **nem tudja
> megkülönböztetni** attól, hogy a router lefagyott – a hálózat mindkét esetben
> ugyanúgy eltűnik. A türelmi idő a hálózat eltűnésétől az ESP saját
> router-resetjéig **2,0 perc** (mérve: `PWR3`). Ha ezen belül visszadugod, az
> ESP **hozzá sem nyúl** a reléhez, csendben visszacsatlakozik (`PWR4`). Ha
> tovább tart, ő is ad neki egy 90 másodperces áramtalanítást – kellemetlen,
> de magától rendbe jön.

### Áramszünet

Áramszünetkor az ESP és a router **együtt** áll le, és együtt is indul – csak
az ESP másodpercek alatt, a router percek alatt. A **10 perces türelmi idő**
(`firstStartDelay`) minden hidegindulásnál megvédi a routert attól, hogy az ESP
épp bootolás közben vegye el az áramát; a **60 mp-enkénti próba** pedig lezárja
ezt a várakozást, amint a router ténylegesen felállt.

| helyzet | mért eredmény |
|---|---|
| a router 3 perc alatt feláll | az ESP **3:21**-kor kezd tesztelni, a reléhez **hozzá sem nyúl** (`PWR1`) |
| a router egyáltalán nem áll fel | az első router-újraindítás **12 perckor** (`PWR2`) |

Az áramszünet **törli** a diagnosztikai naplót és az RTC-számlálókat (tiszta
lap), de a Wi-Fi konfiguráció a LittleFS-en **megmarad**.

### Az AP beállító mód

Az AP mód **5 perc** tétlenség után deep sleepbe megy, **időzített ébresztés
nélkül** – ugyanúgy, mint a végzetes LittleFS hibánál. Visszahozni a reset
gombbal vagy áramtalanítással lehet.

Két védelem gondoskodik arról, hogy a mentés ne vesszen el:

- **Minden HTTP kérés újraindítja az 5 perces visszaszámlálást** – a 404-es
  válasszal végződő is. A határidő abszolút: mindig pontosan 5 perccel a
  *legutolsó* kérés utánra kerül, nem halmozódik, és felső korlát nincs.
- **Fájlírás közben az eszköz soha nem alszik el és nem indul újra**
  (`savingConfig` jelző), így nem maradhat félig kiírt konfiguráció. A leállási
  út nem csak megvárja a folyamatban lévő mentést, hanem **meg is szerzi a
  zárat**, hogy a várakozás vége és a tényleges alvás/újraindítás közötti
  ezredmásodpercekben se indulhasson új írás.

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
| A mentés félbeszakad (megtelt / haldokló flash) | A **régi érték is odavész**: a `FILE_WRITE` már a megnyitáskor csonkol, tehát mire kiderül, hogy az írás nem fér ki, a korábbi tartalom nincs meg. A válasz 500, az eszköz **nem indul újra**, és a következő induláskor üres SSID-vel **AP módba** kerül, ahol újra beállítható (mérve: `FS14`) |
| A fájlrendszer nem csatolható | Konkrét ok kiírása (a `begin()` csak `ESP_FAIL`-nél formáz, hiányzó partíciónál nem), majd végzetes hibajelzés – lásd fent |
| Hiányzó konfigurációs fájl | `CONFIG_MISSING` – nem hiba, AP beállító portál |
| A fájl nem nyitható írásra | `writeConfigValue()` `false`-t ad, a mentés nem történik meg |
| Rövid írás (megtelt FS) | A `print()` visszatérési értéke ellenőrizve, `false` |
| Az írás „sikerült", de a tartalom hibás | **Visszaolvasásos ellenőrzés** fogja meg |
| A törlés (csonkolás) nem megy | Tartalék: a fájl törlése `remove()`-val |
| Mentés a beállító oldalról | Két fázisú: fájl csak akkor íródik, ha **minden** mező érvényes. Hiba esetén HTTP 500 magyarázattal és **nincs újraindítás** |
| Érvénytelen űrlapadat (rossz SSID hossz, IP formátum, hiányzó SSID) | HTTP 500 az ok megnevezésével; **semmi nem íródik ki**, a futó konfiguráció sem változik |
| Mentés, miközben a wifireset gomb épp töröl | HTTP 503, a mentés nem ír bele a törlésbe (atomikus zár) |

A visszaolvasás azért kell, mert a `File::close()` és a `File::flush()` is
`void` a core-ban – a lezáráskor jelentkező hibát másképp nem lehetne észlelni.

A beállító oldalról mentés hibája korábban a legkellemetlenebb módon jelent
volna meg: az eszköz „Done"-t válaszol, újraindul, és mivel nem mentett semmit,
ismét AP módban jön fel – a felhasználó pedig végtelen körben próbálkozik
magyarázat nélkül. Most a hibát megkapja, és az eszköz nem indul újra, tehát a
beírt adatok sem vesznek el.

## 🔒 Biztonság

- A LittleFS-en tárolt Wi-Fi adatok (`/ssid.txt`, `/pass.txt`, `/ip.txt`,
  `/gateway.txt`) **nem érhetők el a webszerveren keresztül** – a portál a
  fájlrendszerről semmit nem szolgál ki, a `/` és a `/log` tartalma is a
  programból megy ki.
- A jelszó soha nem kerül ki nyílt szövegként a soros portra, csak a hossza.
- **A mentett jelszó semmilyen weboldalon nem érhető el**: sem az űrlap
  előkitöltésében (`AP2`), sem a naplóoldalon (`LOG2`), sem a hibaüzenetekben –
  azok fix szövegek, sosem a beírt érték. A beírás `type="password"` mezőben
  történik, `POST` metódussal, tehát a böngésző előzményeibe sem kerül be.
- **Az előkitöltött értékek HTML-escape-elve mennek ki** (`AP3`). Az SSID
  tetszőleges 32 bájt lehet, idézőjelet és `<` jelet is: escape nélkül az
  előkitöltés maga nyitna XSS-t a saját portálunkon.
- Az AP jelszava (`12345678`) a forráskódban van; éles használat előtt
  érdemes lecserélni az `AP_PASSWORD` konstansban. A hosszát `static_assert`
  őrzi (WPA2: 8–63 karakter), mert a core rövidebbnél nem indítana AP-t, és a
  `softAP()` visszatérési értékét nem nézzük.
- Mivel az AP jelszava (a PSK) nyilvános, a WPA2 titkosítás a portál forgalmát
  gyakorlatilag nem védi olyan támadótól, aki a kapcsolódási kézfogást is
  rögzíti – a beállítást ezért ne végezze senki olyan környezetben, ahol a
  rádiótávolságon belüli lehallgatás reális kockázat.

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

## 🗺️ Folyamatábrák

A teljes működés pontos folyamatábrái Mermaid formátumban (a GitHub natívan
rendereli): `setup()`, `loop()` diszpécser, monitor állapotgép, router reset
szekvencia, AP portál, alvás/ébredés:
[FOLYAMATABRA.md](FOLYAMATABRA.md)

## 🔍 Diagnosztikai napló

Az utolsó 32 esemény RTC memóriában, a beállító portál **`/log`** oldalán
olvasható – soros kábel nélkül is. Túléli a deep sleepet, a watchdog resetet és
a reset gombot.

**Az áramszünetet is túléli** – mert a program a fontos pillanatokban kiírja a
naplót a LittleFS-re is (`/evlog.bin`): **router reset előtt**, **AP módba
váltás előtt**, és **minden alvás előtt**. Ezek azok a pontok, ahol vagy
hosszabb idő következik, vagy az eszköz beavatkozik – és mindkettő után könnyen
jöhet egy áramszünet, ami az RTC naplót elvinné.

| | |
|---|---|
| Mikor | router reset előtt, AP módba váltás előtt, és **minden alvás előtt** (egyetlen közös ponton, az `enterDeepSleep()` elején) |
| Írás közben | nincs alvás, újraindulás és másik írás – ugyanaz a **konfigurációs zár**, amit a webes mentés használ; foglalt zárnál a mentés **kimarad**, nem blokkol |
| Sikeresség | **visszaolvasással** ellenőrizve; a sikertelenség **nem végzetes**, az RTC napló ettől még ép |
| Hogyan | **hozzáfűz, nem ír felül**: a fájl megőrzi a régi tartalmát, és a végére kerül, ami a legutóbbi mentés óta keletkezett |
| Mennyit őriz | a fájl **saját, 128 bejegyzéses gyűrű** – négyszerese az RTC naplónak, több áramszünet története is elfér benne |
| A `/log` oldal | **mindkét forrást** mutatja: a fájlt, majd az RTC-ből azt, ami még nincs benne (kettőzés nélkül); a lap a legfrissebb **48 sorra** korlátozódik |
| A teljes napló | a soros porton a **`LOG`** paranccsal |
| Hiányzó / üres / hibás fájl | nem hiba: az RTC naplót mutatja |
| Ha mindkettő üres | egyszerűen nincs napló a lapon |

> **Miért hozzáfűz?** Mert korábban felülírt – és pontosan az áramszünetnél
> tette a legrosszabbat. Az `RTC_NOINIT` terület ilyenkor törlődik, tehát a
> „meddig mentettünk" jelző is nullázódik, és az első mentés az **egyetlen
> friss, még időbélyeg nélküli BOOT** bejegyzést írta a korábbi 32 helyére.
> Mérve: 404 bájt / 32 bejegyzés → 32 bájt / 1. Vagyis épp az az esemény
> tüntette el a tartós naplót, amiért az egyáltalán létezik. (Az `NV20` teszt
> ezt rögzíti.)

> **Miért korlátos a weblap?** Mert a `/log` az **async_tcp** taskon fut, és a
> teljes napló egyben 160 sor = **mért 18 KB**. Egy ekkora összefüggő foglalás
> szembemegy a program elvével: a heap-felügyelet kritikus küszöbe a legnagyobb
> tömbre 6000 bájt. A lap ezért a legfrissebb 48 sort mutatja (**6,9 KB**), és
> kiírja, hogy a teljes napló a soros porton érhető el.

**Valós idő:** amint van kapcsolat – **bármelyik úton** jött is létre –, elindul
az óraszinkron (`hu.pool.ntp.org`, magyar időzóna a nyári időszámítással). **Ha
az NTP nem sikerül, az nem okoz gondot:** az órára csak a bejegyzések kiírása és
a frissesség-döntés tie-breakje épül, és mindkettő működik nélküle (`-` az Idő
oszlopban, illetve darabszám-alapú döntés). Hamis 1970-es dátum sem jelenik meg. Amíg nincs szinkron,
a lap `-`-t ír az Idő oszlopba – a napló ettől még működik. A valós idő a deep
sleepet **túléli**, ezért használható a bejegyzések rendezésére bootolásokon át.

**Nem fájlba megy**, hanem egy körpufferbe az RTC memóriában: nincs flash írás,
nincs naplóírási hiba, amit kezelni kellene, és akkor is olvasható marad, ha
épp a fájlrendszer csatolása bukott meg. Az ismétlődő eseményekből (`TEST
FAIL`, `WIFI LOST`, `STUCK BUTTON`) csak az **első** kerül be, hogy egy tartós
hiba ne söpörje ki a puffert – épp a hiba kezdetét veszítenénk el. Részletek:
[MUKODES.md](MUKODES.md)

## 🧪 Tesztek

A firmware vezérlési logikája hardver nélkül is tesztelhető: a `test/` mappa a
tényleges sketch-et fordítja host gcc-vel, stub ESP32 API-k felett.

```bash
cd test && make test
```

Részletek: [test/README.md](test/README.md)
