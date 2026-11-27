# Tesztek

Ezek a tesztek a **tényleges programot** fordítják le host gcc-vel, stub
Arduino/ESP32 API-k felett. Nem kell hozzájuk ESP32 hardver, sem Arduino
toolchain – csak egy C++17-es fordító.

```bash
cd test
make test
```

> A teljes készlet ~2 perc ágonként; a CI-ban a `make test` + `make san` +
> `make covgate` együtt ~7-8 perc (mindhárom újrafordít és újra lefuttat
> minden forgatókönyvet).

**A fordítási modell megegyezik az Arduinóéval**: a `bazsi_router_reboot.ino`
és a hét modul (`secret.cpp`, `strutil.cpp`, `netprobe.cpp`, `sync.cpp`,
`eventlog.cpp`, `configstore.cpp`, `webportal.cpp`) **külön fordítási
egységek**, amiket a linker fűz össze – pontosan úgy, ahogy az Arduino teszi a
sketch mappa `.cpp` fájljaival. Így egy hiányzó deklaráció vagy egy nem
található szimbólum már itt kiderül, nem csak az eszközön.

> Új modul hozzáadásakor a `Makefile` **`MODULES`** listáját kell bővíteni. Ha
> kimarad, a link azonnal `undefined reference`-szel bukik – a felejtés nem
> tud csendben maradni.

A suite **kétszer** fut: az ESP32 Arduino 4.0.0 **IDF 6**-os ágán, és a 3.3.x
**IDF 5**-ös ágán is – a deep sleep gomb-ébresztés API-ját ugyanis az IDF 6
átnevezte, és a sketch mindkét nevet kezeli.

A memóriahibákat és a definiálatlan viselkedést külön cél fogja meg – ugyanaz
a forgatókönyv-halmaz, ASan + UBSan alatt:

```bash
make san
```

A sorlefedettség megmutatja, mely ágakat **nem futtatja egyetlen forgatókönyv
sem** – oda ugyanis semmi nem néz rá. Fájlonként **és** összesítve mér, tehát
a leggyengébb modul is látszik:

```bash
make cov        # a lefedettség + a nem érintett sorok listája
make covgate    # ugyanaz, de kilépési kóddal (a CI ezt futtatja)
```

A `covgate` küszöbe a `COV_MIN` (alapértelmezés 98,8%). A mérést a
`covreport.py` végzi: a gcov fájlonként számol, tehát a „legelső százalék"
több modul mellett már nem a program lefedettsége lenne.

Egyetlen forgatókönyv futtatása név-előtag alapján (a bináris a `make` után áll elő):

```bash
./build/run-idf6 W3     # csak a "W3: statikus IP ..." eset
./build/run-idf6 S      # minden sleep teszt
./build/run-idf5 SN     # ugyanaz az IDF 5-ös ágon
```

## Hogyan működik

- A `stubs/` a sketch által használt API-k minimális mása. A szignatúrák az
  **ESP32 Arduino 4.0.0** (beágyazott ESP-IDF v6.1) forrásából
  származnak – pl. `typedef NetworkClient WiFiClient`, `size_t File::read(uint8_t*, size_t)`,
  `HTTPClient::setTimeout(uint16_t)`, `Ping.ping(IPAddress, int16_t)`.
- A `Ping.ping()` és a `http.GET()` a **valódi timeoutjukig "blokkol"** a szimulált
  időben (1 mp, illetve 15 mp), etetés nélkül – enélkül a watchdog etetési
  közének mérése semmit nem érne.
- A pin-számok a valódi `variants/XIAO_ESP32C3/pins_arduino.h`-ból jönnek
  (`D0`=GPIO2, `D1`=GPIO3, `D3`=GPIO5, `D4`=GPIO6, `D10`=GPIO10). A tesztekben
  ezek **nevesítve** vannak (`PIN_RELAY`, `PIN_LED`, `PIN_WIFILED`,
  `RELAY_HIGH`/`RELAY_LOW`), hogy egy hardveres átkötés egyetlen sor legyen.
- A stub `millis()`-e **`uint32_t`**, nem `unsigned long`: a hoston az utóbbi 64 bites
  lenne, és akkor a `millis() - start` körbefordulás-biztos idiómák másképp
  viselkednének, mint az ESP32-C3-on (ahol `unsigned long` = 32 bit).
- Az idő szimulált: minden `yield()` 10 ms-ot léptet, így a percekben mérhető
  időzítések ezredmásodpercek alatt tesztelhetők.
- `ESP.restart()` és `esp_deep_sleep_start()` C++ kivételt dob, így a
  visszatérés nélküli útvonalak is ellenőrizhetők.
- **Minden forgatókönyv külön processzben fut** (`fork()`), mert a program
  globális állapota (`testState`, `timing`, `uiFlags`) egyébként átszivárogna
  az esetek között. Ez egyben hű is a valósághoz: minden eset hidegindítás.
- **A tesztek a modulok szerződésén át dolgoznak**, nem az implementációjukon.
  A `sync` és a `configstore` állapota `static` a saját fordítási egységében,
  tehát a tesztek nem is érnék el – `restartRequested()`,
  `configWriteInProgress()`, `filesystemReady()` stb. az útjuk. Ez nem csak
  kényszer: így a tesztek a **viselkedést** rögzítik. Ahol egy belső jelzőre
  fogadtak, azt megfigyelhető következményre cseréltük (pl. „a kapcsolat
  elvesztésével törlődik a jelző" helyett „kapcsolat nélkül nem indul újabb
  szinkron").

## Lefedett esetek

**401 forgatókönyv, 2375 ellenőrzés. Sorlefedettség: 99,50%.**

### A watchdog-etetés globális invariánsa

A program fő állapotgépe nem blokkoló, de a hálózati műveletek **percekig
futhatnak egyetlen `loop()` iteráción belül** – és ez csak azért biztonságos,
mert azokban a ciklusokban kézzel etetünk. Ezt korábban semmi nem kényszerítette
ki, csak a fegyelem: egy új blokkoló ág, amiből kimarad a `feedWatchdog()`,
watchdog-resetet ad, és a hiba csak az eszközön derülne ki.

Ezért **minden forgatókönyv** méri a leghosszabb etetés nélküli szakaszt, és a
harness a végén ellenőrzi. Küszöb: **45 000 ms** – a 90 000 ms-os watchdog fele.
A mérhető legrosszabb valós eset a halott DNS: **35 010 ms** – egy 33 mp-es
HTTP kérés és a két szomszédos ping-próba (`WD13`, `BTN2`).

A mérés három dolgot csinál másképp, mint a naiv változat – mindhárom
számított hiba volt benne:

| | Miért |
|---|---|
| Az **idő múlásakor** mér, nem etetéskor | különben az az út, amelyik *véglegesen* abbahagyja az etetést, sosem regisztrálódna |
| Csak a **ténylegesen eltelt** időt számolja | a tesztek időnként előreugratják az órát (`g_millis = …`); az nem etetetlen szakasz |
| Csak a **`setup()`/`loop()` belsejét** nézi | néhány forgatókönyv szándékosan hív belső függvényt saját ciklusból – az a teszt állványzata, nem a firmware útja |
| A blokkoló stubok (`http.GET()`, `Ping.ping()`, `LittleFS.begin()`) **bejelentik** az idejüket | ezek közvetlenül léptetik az órát, tehát a mérés épp a legfontosabb blokkolásokra volt vak |

Felmentés egyetlen helyen van (`g_wdtGapWaiver`), **indoklással**, és a harness
ki is írja: a `WDT8b` szándékosan modellez kórosan lassú flash-formázást
(51,5 s). A helyes válasz ilyenkor nem a küszöb lazítása – az az összes többi
forgatókönyv védelmét gyengítené.

A DNS-ág 11 mutánsa (kitartás, kontrollkérdés, nullázás, saját lépcső, nem
router DNS, gyorsítás gyanúnál, NXDOMAIN, azonosító, forrás, válaszbit, próbák
száma) mind legalább egy tesztet buktat; kettőt csak a `DN1` és a `DN9`
megerősítése után. A Wi-Fi-lépcső 12 mutánsa (ESP-újraindítás elsőként, a
türelmi idő kihagyása, a stabil ablak, a lépcső, a 48 órás határ, a törlési és
mentési utak, a magic-ellenőrzés) is mind elbukik; négyet csak a `WFL8`–`WFL10`
és a `GWH5` megerősítése után.

A közös reset-lépcső és az állapotoldal 18 mutánsa (az első türelmi idő, a
nullázás, a lépcső-átvitel három adata és egyszeri felhasználása, a Wi-Fi-ág
kezdete és szintje, a csendes időszak mindkét ágon, a DNS-ág nullázása, az
állapotoldal címszűrése, indítása, leállítása és számlálói) mind elbukik; kettőt
csak az `RD15` és a `QH8` hozzáadása után, egy harmadik pedig egyenértékűnek
bizonyult, ezért a feltételt kivettük a kódból.

A kiesés alatti saját hálózat 14 mutánsa (az űrlap előkitöltése, a kapcsoló
értékellenőrzése és mentése, a beolvasás és annak hibája, az indítás a
`wifiFailed()`-ben és a `setup()`-ban, a leállítás, az AP+STA mód megtartása az
újracsatlakozáskor, a címszűrés AP-ága és annak jelzőfeltétele, a beállító mód
átvétele, a jelző be- és kikapcsolása) mind elbukik.

Mutációval igazolva: a `startRouterReset()` pulzus-ciklusából kivett
`feedWatchdog()` **57** forgatókönyvet buktat meg.
Az `initWiFi()` 20 s-es várakozásából kivéve *nem* bukik – és ez helyes:
20 s < 90 s, az nem watchdog-reset.

### Globális invariáns: a kritikus szakasz sosem ágyazódik egymásba

A `portENTER_CRITICAL` az ESP32-n **nem rekurzív** spinlockot vesz fel. Ha
ugyanaz a task másodszor is belép, magára vár — azonnali, teljes megállás, és
mivel a szakasz a megszakításokat is tiltja, még a watchdog megszakítása sem
futna: az eszköz **némán fagyna le**.

Mérve: 280 forgatókönyv éri el az 1-es mélységet, **egy sem a 2-est**. Eddig ezt
három forgatókönyv nézte; mostantól mind. Mutációval igazolva: egy beágyazott
`portENTER_CRITICAL` a `lastEventWas()`-ban **20 forgatókönyvet** buktat meg.

### Statikus ellenőrzések: `make lint`, `make warn`, `make stack`

A futásidejű invariánsok csak a **bejárt** utakat mérik. Egy új, még teszteletlen
ág ott észrevétlen maradna – ezért három ellenőrzés a **forrást**, illetve a
lefordított kódot nézi. Mindhárom mutációval igazolva.

| Cél | Mit tart be | Miért nem volt eddig kikényszerítve |
|---|---|---|
| `make lint` | **(1)** minden `delay()`-t tartalmazó `while`/`for`/`do` ciklus etessen, térjen vissza a `loop()`-ba, vagy kapjon `// WDT-OK: <indok>` jelölést · **(2)** nincs dinamikus foglalás (`malloc`, `new`, STL tároló, érték szerinti `String`) · **(3)** két abszolút időpontot tilos közvetlenül összehasonlítani – csak `(int32_t)(most - határidő)` vagy `(most - kezdet) < időtartam` | a (2) a README és a MUKODES **állítása** volt, nulla ellenőrzéssel; a (3) 49,7 naponta **egyszer** hibázna, és semmilyen rövidebb teszten nem látszana |
| `make warn` | `-Wformat-truncation=2`, `-Wformat-overflow=2`, `-Wstringop-*`, `-Werror` | ezek **csak `-O2` mellett** futnak, a fő teszt-build viszont `-O0` (a pontos lefedettségért) – így csendben kimaradtak |
| `make stack` | az async_tcp taskon futó HTTP kezelők veremkerete (`handleConfigPost` 720 B, `sendDiagnosticLog` 592 B, küszöb 1200 B) | a kód több helyen **épp a véges veremre hivatkozva** választ közös puffert – de a számot semmi nem mérte |

Két `// WDT-OK` jelölés van ma (a 3 s-es beragadt-gomb villogás és a soros port
bevárása, ahol a watchdog még nem is él) – **mindkettő indoklással a forrásban**.

> A `make stack` határai kimondva: ez **host (x86-64) veremkeret**, nem a
> RISC-V-é. A két szám nem egyenlő, tehát nem abszolút garancia. Iránymutatónak
> viszont pontos: egy 2 KB-os puffer a POST kezelőben itt is azonnal látszik
> (mérve: 2768 bájt, a küszöb fölött).

#### Amit szándékosan *nem* tettem globális invariánssá

A **naplóelárasztás** és a **soros kiírás rátája** nem fordítható értelmes
globális szabállyá: a hosszú forgatókönyvek jogosan lépik túl (a `WFL3` két nap
alatt több tucat bejegyzést ír a 32 elemű gyűrűbe), és az alvás a harness-ben azonnali,
ami a percre vetített rátát torzítja. Ott a célzott `LOG*` és `SER*` tesztek a
helyes eszköz – egy rossz globális küszöb csak zajt adna.

| | |
|---|---|
| `W1`–`W9` | Wi-Fi: konfig portál, DHCP vs. statikus IP, DNS, timeout, újracsatlakozás, netif-elvesztés |
| `S1`–`S6` | Deep sleep: beragadt gomb, kimenetek állapota alvás előtt, ébresztőforrások, RTC-láb megkötés, **a relé láb (`D10` = GPIO10) hold-ja alvás alatt és a feloldása ébredéskor** |
| `RL1` | Relé: a 90 másodperces reset pulzus hossza |
| `R5`, `R7` | **A dokumentált idők mérése**: a rossz jelszó csak a 2. próbától látszik; a felismerési idő router hibánál 150 mp (élő DNS) / 168 mp (halott DNS) |
| `H1`–`H8` | HTTP teszt: CR/LF vágás, hibás státusz, captive portal, Content-Length nélküli válasz, beragadt szerver, **204-es ellenőrzés**, a tartalék HTTP teszt végpont-sorrendje |
| `CH1`–`CH5` | **Chunked keretezés**: egy és több darab, darab-kiterjesztés, nagybetűs hexa, szabálytalan és túlcsorduló méret, 50 kB-os darabfolyam |
| `PG1`–`PG3` | Ping: 2-a-4-ből szabály és korai kilépés; az **ESPping 1.0.5 fantom-sikere** (el sem küldött csomagra igaz) bukásnak számít |
| `C1` | Konfig fájlok írása/olvasása, csonkítás, hiányzó fájl |
| `B1` | Gomb debounce: zajtüske vs. tartós nyomás |
| `LAT1`–`LAT7` | **Megszakítás-alapú gombretesz**: blokkoló szakaszba eső rövid nyomás sem vész el; a retesz nem kerüli meg a debounce-t (zajtüske); a wifireset is reteszel; fájlírás alatt a nyomás késik, de megmarad; beragadt gomb nem reteszel; **foglalt zárnál a retesz megmarad**; alvás előtt a megszakítások leválasztva |
| `BTN1`–`BTN3` | **A gombok mintavételezése hosszú várakozás alatt**: a saját ciklusaink 10 ms-onként nézik mindkettőt; a blokkoló `http.GET()` alatti **pollozási** vak ablak mérve (33 010 ms halott DNS-nél, egy kérésnyi) – ezt a `LAT*` retesz hidalja át; a végig nyomva tartott gomb az ablak után is hat |
| `F1`–`F4` | Webszerver: a **programba fordított** beállító űrlap üres fájlrendszer mellett is, **a LittleFS-ről semmit nem szolgál ki** (akkor sem, ha ott vannak a régi fájlok), 404, AP-határidő kitolása |
| `WDT1`–`WDT8` | Watchdog: konfiguráció, etetés a hosszú blokkolások alatt, `delay()` vs. CPU-pörgetés, a feliratkozás tényleges ellenőrzése |
| `SN1`–`SN2` | Biztonsági háló: ha a gomb-ébresztés armolása hibázik, időzítő |
| `SH1`–`SH3` | **Alvás és újraindulás**: a leállás nem csak megvárja a fájlírást, hanem **meg is szerzi a zárat** (az utolsó pillanatban érkező mentés 503-at kap, nem csonka fájlt) – a határidős kilépés mellett; az öt alvási út előfeltételei egymás mellett (időzítő csak oda, ahol a hiba magától elmúlhat; gombébresztés mindenhova a beragadt gombot kivéve); és hogy a **felébresztő gombnyomást nem nézzük beragadt gombnak** (a küszöb mérve) |
| `PWR1`–`PWR4` | **Áramszünet és kézi router-áramtalanítás**: 3 perces router-boot alatt az ESP hozzá sem nyúl a reléhez és 3:21-kor kezd tesztelni; ha a router egyáltalán nem áll fel, az első reset 12 perckor; üzem közbeni kézi áramtalanításnál a türelmi idő **4,3 perc** (közben az ESP egyszer újraindul), és a 90 mp-en belül visszadugott routernél nincs relé-impulzus |
| `BNC1`–`BNC4` | **Gombpattogás**: a pattogó nyomás egyszer reteszel és nem hagy hátra „félig lenyomott" állapotot (különben egy későbbi magányos felfutó él spontán újraindítást okozna); a pattogó tüske nem kerüli meg a debounce-t; a folyamatosan recsegő, kopott gomb **nem** indít újra; a beragadt-gomb ellenőrzés egyetlen beolvasásból dönt |
| `SER6`–`SER7` | **A soros port életciklusa**: 115200 baud, az első sor csak a CDC beállása után megy ki, alvás előtt `flush` majd `end` **a legvégén**, és a lezárás után egyetlen sort sem írunk – a beragadt gomb ágát is beleértve |
| `SER8`–`SER9` | **Ellenséges soros terhelés**: a küszöb körül ingadozó heap (13 sor/perc) és a mindig bukó naplómentés tartós kiesés alatt (22 sor/perc) – mindkettő a 30 sor/perces költségvetés alatt |
| `HP10` | **A `LOW HEAP` bejegyzés spam-védelme**: a küszöb körüli ingadozás védelem nélkül 30 perc alatt **32 bejegyzést** írt (a teljes körpuffert kisöpörve), a `lastEventWas()` után **1** |
| `AP1`–`AP4` | **AP portál űrlap**: az üres címmező törlést jelent (a DHCP-re váltás útja); az előkitöltés **soha nem tartalmazza a jelszót**; az SSID **HTML-escape-elve** kerül a lapra (XSS ellen); az előkitöltéssel a statikus IP megmarad jelszócserénél |
| `LOG1`–`LOG3` | **Naplóoldal**: emberi olvasásra készül (szöveges reset ok, nap/óra/perc/mp uptime, `Param` jelmagyarázat, ami nem ütközik a táblázatcella-mintával); **semmilyen konfigurációs érték nem jelenik meg**; a körpuffer körbefordulása után is pontosan 32 sor |
| `LOG4`–`LOG5` | **A napló írása**: pislákoló Wi-Fi mellett egy `WIFI LOST` sorozatból csak az első kerül be (ugyanaz a szabály, mint a `TEST FAIL`-nél), a soros port sem árad meg; a naplózás **nem nyúl a fájlrendszerhez** (nem hoz létre fájlt, üres tárral is működik) és **nem ágyaz kritikus szakaszt kritikus szakaszba** |
| `LOG6` | **A napló túlcsordulása**: az írási pozíció körbefordul a 32 biten, de a méret kettő hatványa, ezért az index folytonos marad (a 40 írásból mind a 32 legutóbbi megvan, egyik sem kétszer); a `/log` a fordulás után is ép, és egyik olvasó sem indexel ki |
| `FS11`–`FS14` | **Fájlkezelés hibainjektálással**: sorvégek (CRLF) és csupa-whitespace tartalom, bináris szemét és beágyazott NUL, a puffernél hosszabb fájl (csonkolás túlcsordulás nélkül), menet közben megtelő fájlrendszer (a **zár felszabadul**, a gombok tovább működnek), és a félbeszakadt mentés rögzített viselkedése |
| `FS1`–`FS10` | LittleFS hibák: csatolás, írásvédettség, megtelt tár, **néma írási hiba**, csonka olvasás, törlés tartalék útvonala |
| `FT1`–`FT8` | Végzetes hiba: betölthetetlen konfig vs. „nincs még konfig", LED-villogás, gombok |
| `WF1`–`WF6` | Csatlakozási hiba: hidegindulásnál 10 perc türelem + 3 próba, majd router reset (12 perc); működés közben 3 próba, majd ESP-újraindítás |
| `WFL1`–`WFL12` | **A Wi-Fi-lépcső**: az újraindítás után visszajövő Wi-Fi → nincs router reset, nincs 10 perces türelmi idő; ha nem jön vissza → router reset 5,3 perc után; tartós hiány → 30 perc / 1 / 2 / 4 óra lépcső, 48 óra után AP, alvás nélkül; várakozás közben percenként 1 próba, a visszajövő Wi-Fi-t azonnal észreveszi; instabil Wi-Fi → a második kiesésre router reset; rossz jelszó → AP; áramszünet törli a lépcsőt; a valódi heap-újraindulás percre pontosan átviszi; a kiesés nullázza a stabil kapcsolat óráját; a wifireset, a mentés utáni újraindulás és az alvás törli; ha a reset után sem jön vissza a Wi-Fi, az ESP-újraindulás után a lépcső folytatódik (a második reset 30 perc után, nem ~15 perc után), és ha ott a Wi-Fi visszajön, de az internet nem, az internet-ág sem resetel idő előtt |
| `WD1`–`WD13` | Ismétlődő watchdog újraindulás; a leghosszabb etetés nélküli szakasz minden üzemmódban, a **halott DNS** legrosszabb esetét is beleértve |
| `WDT14`–`WDT15` | **Az „1 óra hibátlan működés” mihez képest mér**: a mostani indulás kezdetéhez, nem abszolút `millis()` értékhez – nagy kezdő `millis()` mellett és a **körbefordulás** átlépésekor is (mindkét mérés elbukik, ha valaki visszanézi abszolút alakra) |
| `HP1`–`HP9` | **Heap felügyelet**: az állapotsor félóránként megy ki (nem körönként), a figyelmeztetés csak a küszöb átlépésekor; egyetlen mélypont **nem** indít újra, tartós kritikus szint igen; a router reset számláló **túléli** az újraindulást és pontosan egyszer használódik fel; AP módban, relé-impulzus és fájlírás közben nincs újraindulás; három sikertelen kör után `MODE_FATAL`, nem boot loop; és egy valódi lassú szivárgás végponttól végpontig |
| `GWH1`–`GWH5` | **Mi marad meg egy heap-újraindulás után**: a router reset utáni **ellenőrző ablakban** nincs önkéntes újraindulás (a gateway-eszkaláció kétfázisú, és a fázisokat összekötő állapot nem vinne át) – mutációval ellenőrizve; az ablak végén a döntés rendesen megszületik (AP mód a helyes indoklással); a teljes leltár arról, mi vész el, és miért olcsó újraszámolni; gombnyomásnál a Wi-Fi-lépcső is tiszta lappal indul |
| `NV1`–`NV11` | **A napló mentése a fájlrendszerre**: kimegy a három fontos pillanatban (és a relé kapcsolása **előtt**); az írás sikerességét visszaolvasás ellenőrzi, a néma írási hibát is elkapva; a mentés atomikusan szerzi meg a zárat és fel is oldja, foglalt zárnál kimarad; mentés közben nincs gombos újraindulás és másik írás; a lap a **frissebbet** tölti be (élő RTC → RTC, áramszünet után → fájl); hiányzó, üres, csonka, rossz magic-ű és hazudós fejlécű fájl **egyike sem okoz gondot**, mindkettő üresnél nincs napló a lapon; NTP időbélyeg; a félúton bukó betöltés nem hagy kevert puffert; **minden alvás** menti a naplót (nem csak az időzített), és csatolatlan fájlrendszernél magától kimarad; végül az **óraszinkron minden kapcsolati úton elindul** – a korai kilépéseken is, ahol az `initWiFi()` nem fut le; és **sikertelen NTP mellett** a teljes eszkaláció végigfut, a `/log` a darabszám-alapú szabályra esik vissza, `-` áll az Idő oszlopban, és sehol nincs hamis 1970-es dátum |
| `NV12`–`NV16` | **A naplómentés hibaútjai**: megtelt fájlrendszer (rövid írás), a **flash kímélése** (változás nélkül nem írunk újra), a „mentve: …" időbélyeg, és az olvashatatlan fájlrendszer három ága – írás után lehetetlen visszaolvasás, megnyithatatlan naplófájl a lapon, félbeszakadó fejléc-átugrás |
| `NV15`, `AP5`–`AP7`, `WF11` | **A lefedettség-mérésből előkerült vakfoltok**: mind a **10 indulási ok** emberi neve a `/log` oldalon (plusz a nem nevesített `ESP_RST_USB`); **aposztróf** és társai az SSID-ben (a HTML-escape ezen ága sosem futott le); mentés csatolatlan fájlrendszerrel → tiszta 500; a „router reset után sem jött vissza a WiFi" hibaág; és hogy **mind a négy mező egyformán tűri** a beillesztett szóközöket (a határértékes érték záró szóközzel is mentődik, a túlméretes viszont továbbra is hiba) |
| `LOG7`–`LOG9` | **A naplózás változásainak utóhatásai**: a megnőtt `/log` oldal (Idő oszlop, Forrás sor, két új eseménykód) a legrosszabb esetben – tele körpuffer, valós időbélyegekkel – is **4546 bájt**, tehát belefér a stream 6144 bájtos kezdő pufferébe; egy soros porton keresztüli firmware frissítés után a **régi elrendezésű** RTC napló érvénytelen (a magic egyben verziójelző); és a `wifireset` a naplófájlt szándékosan **nem** törli |
| `CC1`–`CC3` | **Két task, egy memória**: a portál futása alatt a `loop` egyetlen `WiFi.begin()`-t sem ad ki (tehát a konfigurációs puffereket nem olvassa), miközben mentések érkeznek; ugyanez a `MODE_FATAL` ágon, ahol a webszerver **még fut**; és két mentés között a négy puffer mindig **együtt** vált át |
| `E1`–`E6` | Végponttól végpontig: egészséges ciklus, router reset, AP mód, gombok, visszatérő WiFi |
| `P1`–`P20` | Beállító portál mentése: **két fázisú commit** (hibás mezőnél semmi sem íródik), validáció, írási hiba, jelszó-szivárgás, határidő, IP+gateway páros, whitespace (az IP/gateway vágását is beleértve), mentés közbeni gombnyomás, halasztott újraindítás, **503 zárolt konfignál** |
| `CPU1`–`CPU2` | A loop() nem pörgeti a CPU-t a várakozó állapotokban |
| `X1`–`X14`, `X2b` | Határesetek: gomb a relé pulzus közben, nyílt hálózat (üres jelszó + jelölő; az üres jelszómező a mentett jelszót megtartja; jelölő + jelszó → 500), SSID/jelszó határértékek, wifireset törlési sorrend és **sikertelen törlés → végzetes hiba**, query-paraméter, 1 napnál hosszabb uptime |
| `PO1`–`PO3` | Áramszünet: mekkora router-indulási késést tolerál |
| `R1` | Rossz jelszó → AP mód router reset nélkül, a lépcső el sem indul |
| `L1`–`L7` | Diagnosztikai napló: rögzítés, /log oldal, körpuffer, spam-védelem, esemény-kódok, **mind a 17 címke**, üres napló |
| `SB1`–`SB5` | Beragadt gomb: mindkét gomb, váltakozó LED-villogás, naplózás, **az ismétlődő kör spam-védelme** |
| `SER1`–`SER5` | Soros kimenet: terhelés (nem árasztja el a konzolt), kiesésnél az eltelt és a hátralévő perc, sikernél csak `Successful Test`, eltérésnél a kapott törzs is |
| `OV1` | A reset-számláló pontosan követi a reseteket több cikluson át |
| `IP1`–`IP3` | Csak IPv4 fogadható el (IPv6 és `0.0.0.0` nem) |
| `LED4`–`LED6` | **LED-ek őszintesége**: a Wi-Fi LED legfeljebb egy percet késhet (mérve: 58 mp), a státusz LED normál üzemben végig világít, AP módban a mentés utáni türelmi idő alatt is villog |
| `WDT8b` | **A LittleFS formázása** is belefér a watchdog ablakába: 7,5 mp tipikus és 51,5 mp rossz eset, mindkettő a 90 mp alatt |
| `RR3` | egyetlen sikeres ellenőrzés nullázza a reset-számlálót és a kiesést |
| `RD1`–`RD15` | **A reset-döntés** (funkcióteszt, percre bontott menetrenddel): router hiba → reset kb. 2 perc után; 5–14 perces szolgáltatói kiesés → nincs reset; 6 órás kiesés → 4 reset a 15 perc / 30 perc / 1 óra / 2 óra lépcsőn; **a 2026-11-22-i terepi nap → 0 reset**; pingre nem válaszoló router → router hibának látszik; szűrt ping, működő HTTP → nincs reset, az öt végpont sorra kerül; gyógyulás után a lépcső elölről; egy napig halott router → 9 reset, alvás nélkül; heap-átvitel; gateway-cím nélkül nincs vak ping; Wi-Fi-vesztés a kiesésen belül a lépcsőt követi; reset után rossz jelszó → AP mód; az 1.1.1.1 hallgat, a 8.8.8.8 válaszol → nincs reset; percenként pontosan egy ellenőrzés; heap-újraindulás az első reset előtt → a türelmi idő folytatódik, nem indul elölről |
| `CP1`–`CP5` | **A mentés utáni első indulás**: ha a mentett hálózat nem érhető el → AP mód (`AP MODE` 5) kb. 2 perc után, router reset nélkül; ha sikerül csatlakozni, a jelző törlődik, és egy későbbi kiesés már a megszokott lépcső (ESP-újraindítás, reset); a mentés utáni AP lejártakor nem alszik el, hanem újraindul és a megszokott szabály szerint figyel (végül reset); rossz jelszó → `AP MODE` 2, szintén reset nélkül; áramszünet után nincs próbajelző |
| `OA1`–`OA7` | **Kiesés alatti saját hálózat**: a kapcsoló az űrlapon (alapból ki; rejtett 0 + jelölőnégyzet; hibás érték → 500, nem ír; a mező nélküli POST nem változtat); induláskor „1” = be, „0”/hiány = ki, olvashatatlan fájl → ki (nem végzetes); kikapcsolva nincs AP; bekapcsolva az ESP-újraindítás után AP+STA (3,3 perc), az állapotoldal a 192.168.4.x-ről elérhető, máshonnan 403, az újracsatlakozási próbák nem kapcsolják ki, a router reset ugyanúgy 5,3 percnél jön, a Wi-Fi visszatérésekor leáll; a beállító AP mód átveszi a rádiót; hidegindulásnál a türelmi idő után indul; ha a saját hálózat nem indul el, nincs jelző, STA módban marad, és a következő próba újra megkísérli |
| `ST1`–`ST7` | **Az állapotoldal**: normál üzemben fut, a helyi hálózatról 200 (számlálók, események, se SSID, se jelszó, se űrlap, se POST); más alhálózatról és Wi-Fi nélkül 403; kiesés közben a hiba és a resetszám látszik; AP módban leáll, ismeretlen út 404; csak az utolsó 20 esemény, ~2,5 kB; Wi-Fi-kiesés közben elérhetetlen (403), utána „rendben (nemrég kiesett)”, 30 perc stabil kapcsolat után „rendben”; a saját hálózatról kiesés közben „nincs kapcsolat, N perce”; DNS-reset után, kiesés nélkül az Internet sor 0 resetet mutat |
| `QH1`–`QH8` | **Éjszakai csendes időszak**: a 22:00 és 05:00 határ téli és nyári időben, a setup() a magyar időzónát állítja be, érvényes idő nélkül nappali szabály; éjjel router hiba → reset 10 perc után; 8 perces router-kiesés → éjjel nincs reset, nappal van; 05:00 után újra 2 perc; a szolgáltatói kiesés éjjel is 15 perc; éjjel a Wi-Fi miatti router reset legalább 10 perc kiesés után (az ESP-újraindítás előtte); a várakozás alatt visszajövő Wi-Fi → nincs reset; hidegindulásnál (érvényes idővel) a Wi-Fi miatti reset is vár |
| `DN1`–`DN12` | **A router DNS-ének ellenőrzése**: a router DNS-e hallgat, a ping és az `1.1.1.1` DNS megy → reset legkésőbb ~16 perc; a külső DNS is hallgat → nincs reset; 9 perces zavar → nincs reset; a 11-22-i nap DNS-zavarral → 0 reset; tartós hiba → saját lépcső (a ping nem nullázza); ha a DNS nem a router → kimarad; a válasz ellenőrzése (azonosító, forrás, SERVFAIL, NXDOMAIN, csonka, hallgatás: pontosan 2 × 3 mp); a kérdés formátuma az lwIP szerint; gyógyulás után elölről; statikus IP; 5 percenként egy kérdés; ping nélkül szünetel |
| `HC1`–`HC2` | A **HTTP hibakód** naplózható alakja (1000 + \|kód\|) és az első bukáskor a naplóba kerül |
| `FB1`, `SL1` | DHCP mellett **tartalék DNS** (1.1.1.1 a fallback helyen, a router DNS-ét nem írja felül); a Wi-Fi **modem-alvás kikapcsolva** a csatlakozás előtt |
| `WL1`–`WL2` | Wi-Fi-vesztés **rossz jelszóval** → AP mód, router reset nélkül; jelszóhiba nélkül marad a reset |
| `SP1`–`SP3` | **Statikus IP pár**: azonos /24, nem .0/.255, nem azonos; régi rossz konfig → DHCP; a portál nem menti |
| `PW1` | A jelszó vezető/záró szóköze megmarad, a CR/LF nem |
| `APP1`–`APP6` | **Eszközönként egyedi AP jelszó**: 12 karakter, a rádió bekapcsolása után generálva, kódolva mentve, újraindulás és wifireset után is ugyanaz, egyedi AP név, sérült fájl → új jelszó, nem írható fájlrendszer mellett is indul |
| `SERL` | A soros `/log` parancs is működik |
| `BTN4` | **A reset gomb is atomikusan szerzi meg a konfigzárat** újraindítás előtt (regresszió: korábban csak a gyors jelző-ellenőrzést végezte, a `wifiresetbutton()`-nal ellentétben) |
| `SE11` | Az `onlineProbe()` `WiFi.begin()`-je is a **nyílt** jelszót adja, nem a `v1:` kódolt alakot |
| `WF10` | A `RESET_DELAY` korai kilépése után a **statikus IP/DNS érintetlen** – a próba nem hív `config()`-ot, ezért ez az invariáns kötelező |
| `CFG2` | Sérült kódolt jelszó → **nem** végzetes hiba, AP módban javítható |
| `WDT9` | **1 óra hibátlan működés AP módban is nullázza** a watchdog számlálót (regresszió: korábban csak monitor módban) |
| `LED1`–`LED3` | LED-jelzések: a reset pulzus alatt a **státusz LED villog** (2 Hz) és a Wi-Fi LED sötét, **AP módban** a Wi-Fi LED villog (1 Hz) és a státusz LED végig világít, a villogás üteme és határa |
| `OP1`–`OP7` | **Korai kilépés a hosszú várakozásokból**: a `firstStartDelay` korán zárul, ha a hálózat *és* az internet is megvan; csak hálózatnál végigfut; ép induláskor az első próba azonnal fut; **flash kímélés** (nincs fájlírás, percenként max egy `WiFi.begin()`, kapcsolat nélkül nincs ping); a `RESET_DELAY` korai zárása nem bontja le az igazolt kapcsolatot; **a próba órái túlélik a `millis()` körbefordulását**; **élő Wi-Fi mellett is korán zárul, ha az internet menet közben jön vissza** |
| `WDT7` | A watchdog már a LittleFS csatolása **előtt** élesedik |
| `EVT1` | A `/log` `TEST FAIL` paramétere is 1-alapú – ugyanaz a szám, mint a soros porton |

A név-előtag **prefix**, nem pontos egyezés: a `P1` így a `P1`, `P10`–`P19`
eseteket is futtatja.

### Ami szándékosan fedetlen

A `make cov` két sort jelez, mindkettő a **két task közötti valódi verseny**
védőága, amit egyszálú host-futás nem tud előállítani:

| Hol | Miért nem érhető el egyszálú futásban |
|---|---|
| `resetbutton()` reteszelt ágának `return`-je | a `restartFromButton()` csak akkor tér vissza, ha a konfigzár foglalt – a gyors kapu ugyanezt a jelzőt nézi mikroszekundumokkal korábban |
| `wifiresetbutton()` reteszelt ágának `return`-je | ugyanaz a `doWifiReset()`-tel |

Minden más sor lefut: a korábban szerkezetileg elérhetetlen záró `return`-ök és
védelmi ágak (a `readChunked()` darabszám-korlátja, a `testInternetPing()` záró
`return`-je, a `localtime_r` hibaága, a jelszókódolás puffer-hibája) a kódból
kikerültek.

## Fontos

A stubok **nem** emulálják az ESP32-t. Azt ellenőrzik, hogy a firmware
vezérlési logikája, időzítései és API-használata helyes-e. A valódi rádió,
lwIP, LittleFS és a relé viselkedését hardveren kell igazolni.

## Véletlen állapotgép-bejárás (PROP1–PROP8)

A többi forgatókönyv **kézzel írt**: pontosan azokat az utakat járja be, amikre
a szerző gondolt. A PROP forgatókönyvek fordítva dolgoznak: véletlen környezeti
eseménysorozatot generálnak (Wi-Fi-szakadás, gombnyomás tetszőleges
pillanatban, HTTP-válasz, heap-esés, fájlrendszer-hiba), és nem azt állítják,
hogy MI TÖRTÉNJEN, hanem hogy mi **nem történhet soha**:

| | Tulajdonság |
|---|---|
| P1 | a relé sosem marad behúzva a pulzusnál tovább (a router áram nélkül) |
| P2 | az `endpointIndex` sosem lép ki a `TEST_ENDPOINTS[]` táblából |
| P3 | a Wi-Fi-lépcső nem éli túl a 48 órás határt (addigra AP mód) |
| P4 | az eseménynaplóban csak érvényes eseménykód áll |
| P5 | a nyílt szövegű jelszó soha nincs egyetlen fájlban sem |
| P6 | a nyílt szövegű jelszó soha nem kerül a soros kimenetre |
| P7 | a `MODE_CONFIG` / `MODE_FATAL` terminális (csak újraindulással van kiút) |

**Determinisztikus**: a magok fixek, a CI-ban mindig ugyanaz a nyolc bejárás
fut. Egy véletlenszerűen bukdácsoló teszt használhatatlan lenne. Helyben tovább
lehet keresni ugyanezzel a készlettel:

```
RNDSEED=12345 RNDSTEPS=2000000 build/run-idf6 PROP
```

**Az elérés is mérve van** (`[eleres]` sor), és követelve: minden bejárásnak el
kell jutnia a program valamelyik érdemi ágáig. Ez nem öncél – a fejlesztés
közben **kétszer** fordult elő, hogy egy tulajdonság üresen ment át:

1. Az első változat a `loop()` hívások **között** mintavételezte a relé lábát,
   és így sosem látta HIGH-on, mert a 90 mp-es pulzus egyetlen `loop()`
   iteráción belül zajlik le. A mérés ezért a stubba került (`g_relayMaxHighMs`),
   ugyanoda, ahová a watchdog-résé.
2. Két tulajdonság maga volt hibás: az `rtcEvNext` **monoton számláló**, nem
   gyűrűindex, a `terminalSeen` jelzőt pedig nem nullázta az újraindulás.
   Nyolcból nyolc, illetve nyolcból öt bejárás „bukott” – egyik sem a program
   miatt.

Mutációval igazolva, mind a négy a **helyes** tulajdonságot buktatja:

| Mutáció | Elkapja |
|---|---|
| `RESET_PULSE` 90 → 200 mp | P1, 8/8 bejárás |
| `MAX_CYCLE_INDEX` 4 → 5 | P2, 8/8 |
| a jelszó kiírása a soros portra | P6, 8/8 |
| a jelszó kiírása egy fájlba | P5, 8/8 |

## Fuzzing: `make fuzz` (clang libFuzzer)

Minden más teszt – a kézzel írt forgatókönyvek és a 8 véletlen bejárás is –
**érvényes vagy legalábbis elképzelt** bemenetekkel dolgozik. A fuzzer nem:
kifejezetten olyan bájtsorozatokat keres, amikre senki nem gondolt, és a
lefedettség alapján tanulja meg, merre érdemes menni.

| Célpont | Mit elemez | Ki írja a bemenetet |
|---|---|---|
| `fuzz-post` | a POST űrlap-elemző | **bárki, aki AP módban csatlakozik** |
| `fuzz-secret` | `decodeSecretInPlace()` / `encodeSecret()` | a `/pass.txt` (sérült vagy átírt) |
| `fuzz-evlog` | az `/evlog.bin` betöltő | a naplófájl (félbeszakadt írás) |
| `fuzz-config` | `readConfigValue()`, `fileMatches()` | a négy konfigfájl |
| `fuzz-dns` | `dnsResponseOk()`, `buildDnsQuery()` | **a hálózat**: a router (vagy bárki, aki UDP-t küld az eszköznek) |

A **`fuzz-post` és a `fuzz-dns` a lényeg**: ezek túloldalán ülhet idegen fél.
A másik három olyan adatot olvas, amit maga az eszköz írt ki – ott a sérült
flash a reális fenyegetés, nem a rosszindulat.

Mind az öt **ASan + UBSan alatt** fut: a fuzzer önmagában csak az összeomlást
látná, a csendes puffertúlcsordulást nem.

```
make fuzz               minden célpont 30 mp
FUZZSEC=600 make fuzz   hosszabban
build/fuzz-post -runs=100000
build/fuzz-post crash-<hash>    egy találat újrajátszása
```

**Mutációval igazolva, hogy a négy harness tényleg eléri a kódot** – egy
fuzz-teszt legveszélyesebb hibája az, ha nulla találattal fut, mert nem jut el
sehová:

| Mutáció | Eredmény |
|---|---|
| a POST-elemző `candidate` puffere 16 bájt + `strcpy` | **elkapva** 60 mp alatt (stack-buffer-overflow) |
| az evlog-betöltőből kivéve a saját `count` határellenőrzés | **elkapva** 60 mp alatt |
| `decodeSecretInPlace`: túlcsordulás CSAK a `v1:9f` bemenetnél | **elkapva** 50 mp alatt (a fuzzer maga találta meg az előtagot) |
| `readConfigValue`: túlcsordulás CSAK egy adott pufferméretnél | **elkapva** 50 mp alatt |

Egy ötödik mutáció **nem** bukott el, és ezt is kimondjuk: a `d.ssid` túlírása a
`ConfigDraft` **struktúrán belül** marad, azt pedig az ASan alapértelmezésben
nem látja. Ez a módszer valós határa, nem a harnessé.

### A mérés határa

Ez **hoston** fut, stub Arduino API-k felett. Amit megfog: puffertúlcsordulás,
határon túli olvasás, definiálatlan viselkedés, végtelen ciklus – **a saját
kódunkban**. Amit nem: a valódi ESPAsyncWebServer / lwIP hibáit (nem a mi
kódunk, és nem is futnak itt), és az `IPAddress::fromString()`-et sem, mert az a
hoston stub – azt fuzzolni a saját stubunk tesztelése lenne, nem a firmware-é.

## A két task tényleges összefonása (ILV1–ILV5)

A program **két taskon** fut: a loop task mér, dönt és alszik, az async_tcp
task futtatja a HTTP kezelőket. A két task közös állapoton dolgozik
(`savingConfig`, `restartPending`, `restartAt`, `apDeadline`), és épp ezért van
a `portMUX` spinlock. Azt már bizonyítottuk, hogy a kritikus szakasz sosem
ágyazódik egymásba – ezek a forgatókönyvek azt mérik, hogy a két task
**tetszőleges pontokon váltakozva** is helyesen viselkedik.

**Hol vannak a valódi megszakítási pontok?** Egyetlen magon a taskváltás nem
bárhol történik, hanem ott, ahol az ütemező szóhoz jut:

| Horog | Irány | Mit modellez |
|---|---|---|
| `g_onDelay` | loop() → HTTP kezelő | a loop blokkoló szakaszai; az async_tcp magasabb prioritású (3 vs 1), tehát bevág |
| `g_onFsWrite` | HTTP kezelő → loop() | a flash-írás ideje; ilyenkor az async_tcp **már a zár birtokában van**, de még nem engedte el |

| | Mit rögzít |
|---|---|
| ILV1 | `/ping` a loop **minden** `delay()`-ében (3999 összefonás) – a keep-alive a versenyben is hat |
| ILV2 | `/log` olvasás a napló írása közben – a gyűrű ép marad |
| ILV3 | POST mentés a loop közé ékelve; az injektálás abbahagyása után a halasztott újraindítás **pontosan egyszer** fut le, a zárat megszerezve |
| ILV4 | a loop a **flash-írás közben** – a lejárt AP határidő ellenére sem alszik el |
| ILV5 | mindkét irány egyszerre, véletlen sűrűséggel |

### Amit az ILV4 megmutatott

A cél annak mérése volt, hogy a mentés alatt elalvó loop átmegy-e a
`lockConfigBeforeShutdown()`-on. **Nem alszik el, és nem is jut el a zárig**: a
`handleConfigMode()` már a döntésnél kizárja.

```cpp
if (!configWriteInProgress() && !restartRequested() &&
    (int32_t)(currentMillis - apDeadline) >= 0) { apSleep(); }
```

A védelem tehát **kétrétegű**: az első itt van (a loop rá sem lép az alvás
ágára), a második a `lockConfigBeforeShutdown()`, ami azokat az utakat fogja
(gomb, watchdog), amelyek nem ezen a feltételen át jönnek. Az ILV4 az első
réteget rögzíti, és külön ellenőrzi, hogy az ablak tényleg **nyitva volt** (a zár
állt, a határidő lejárt, a loop tényleg futott), különben az állítás üres lenne.

### Mutációval igazolva

| Mutáció | Elkapja |
|---|---|
| a `!configWriteInProgress()` feltétel kivéve az alvás-döntésből | **ILV4** (és a kilépés tényleg bekövetkezik) |
| a halasztott újraindítás határideje sosem jár le | **ILV3** |
| beágyazott `portENTER_CRITICAL` a naplózásban | **10 ellenőrzés** az ILV-kben |

## A stubok igazolása: `make stubcheck`

**A teljes host-tesztkészlet érvényessége ezen áll.** Mind a 2375 ellenőrzés a
`test/stubs/` alatti modelleken keresztül lát: ha egy stub téved, a teszt
**zöld**, az eszköz meg hibázik – és semmi nem szólna.

Mennyire éles ez? A relé a **D10**-en van. Ha a XIAO_ESP32C3 variánsában a D10
száma valaha megváltozik, a sketch továbbra is lefordul, minden teszt zöld marad
(a teszt is a saját konstansát használja), és az eszköz **egy másik lábat**
kapcsolgatna.

Amit összevet (66 állítás):

| | |
|---|---|
| lábkiosztás | D0/D1/D3/D4/D10 a valódi `variants/XIAO_ESP32C3/pins_arduino.h`-ból |
| `wl_status_t` | a modellezett értékek a valódi `WiFiType.h`-ból |
| `esp_reset_reason_t` | a felsorolás **sorrendje** (az értékek implicitek, egy beszúrt elem mindent elcsúsztatna) |
| `esp_task_wdt_config_t` | mezőnevek és típusok, sorrendben |
| `ESP_OK` / `ESP_FAIL` / `ESP_ERR_*` | a modellezett kódok |
| `HTTPClient` | `setTimeout(uint16_t)`, `setConnectTimeout(int32_t)` |
| `ESPAsyncWebServer` | `params()`, `getParam(size_t)`, `name()`/`value()` **referenciát** ad, `isPost()` |
| **viselkedés** | `STA.cpp`: az AUTH_FAIL ág `!first_connect` feltétele – a `WifiSim::authFail` modellje ezen áll |
| **viselkedés** | `main.cpp`: a loopTask minden iterációban etet, ha fel van iratkozva – a `coreLoopStep()` ezt modellezi |
| Wi-Fi | `setSleep(bool)`, `WiFi.STA.dnsIP(uint8_t, IPAddress)` (a `STA` tag publikus), `gatewayIP()` |
| **viselkedés** | `NetworkInterface::gatewayIP()`: netif nélkül 0.0.0.0, különben a netif átjárója – a diagnózis erre épül |
| `IPAddress` | `operator[](int) const` (a statikus IP pár ellenőrzése) |
| ESPping | `averageTime()` típusa, és az 1.0.5 fantom-sikerének oka (`_success = _expected_count - _errors`) |
| `esp_random()` | szignatúra, és a dokumentált feltétel: bekapcsolt Wi-Fi mellett valódi véletlen |
| `NetworkUDP` | `begin(uint16_t)`, `beginPacket(IPAddress, uint16_t)`, `endPacket()`, `parsePacket()`, `clear()`, `remoteIP()`, `remotePort()`; és a **viselkedés**: a `parsePacket()` nem blokkol (`MSG_DONTWAIT`), beolvasatlan csomag mellett 0-t ad, a `clear()` eldobja a maradékot (a 4.0.0-ban a `flush()` elavult) |
| DNS-cím | `WiFi.dnsIP(uint8_t dns_no = 0)`, netif nélkül 0.0.0.0; `IPAddress::operator==` / `!=` |

A **viselkedési** állítások azért vannak benne, mert a program döntései és a
dokumentáció indoklása ezekre épül. Ha azok elmozdulnak, a rájuk épülő
indoklás is érvényét veszti.

A CI a **firmware-build** jobban futtatja: az az egyetlen hely, ahol a pontosan
kitűzött verziók a lemezen vannak. Helyben:

```
STUBROOTS="$HOME/.arduino15 $HOME/Arduino/libraries" make stubcheck
```

**Mutációval igazolva** – hét különböző elcsúszást kap el:

| Mutáció | Elkapja |
|---|---|
| a relé lába 10 → 11 a tesztben | igen |
| `WL_CONNECT_FAILED` 4 → 5 a stubban | igen |
| egy `ESP_RST_` elem kiesik (a többi elcsúszik) | igen, a **2.** indextől |
| `bool trigger_panic` → `int` | igen |
| az `isPost()` eltűnik a stubból | igen |
| a **valódi** header formája változik (a minta nem illeszkedik) | igen – „NEM SIKERULT KIOLVASNI”, nem csendes átmenet |
| hiányzó valódi forrás (rossz gyökér) | igen – „NEM TALALHATO” |

Ezen felül **üresség-védelem**: 24 állítás alatt a futás megbukik. Egy
ellenőrző, ami azért „tiszta”, mert nem vizsgál semmit, rosszabb a semminél.

## A maradék hibaágak (COV1–COV6)

| | Mit fed le |
|---|---|
| COV1 | `eventName()` ismeretlen kódra (`"?"`) – a napló túléli a firmware-frissítést is |
| COV2 | a napló-betöltő **saját** határellenőrzése, a hívótól függetlenül |
| COV3 | a naplófájl eltűnik a fejléc és a bejegyzések olvasása **között** |
| COV4 | **könyvtár** egy konfig útvonalon |
| COV5 | a wifireset foglalt zárnál nem töröl és nem indít újra |
| COV6 | a statikus gateway-ellenőrzés a `WiFi.gatewayIP()` alapján (kapcsolat nélkül 0.0.0.0 → elérhetetlen, vakon nem pingel) |

A COV4 kedvéért a stub megtanult **könyvtárat** modellezni (`g_fsDirs`). Egy
stub, ami egy valós állapotot nem tud előállítani, csendben csökkenti a tesztek
erejét: az az ág addig lefedhetetlen volt.

A COV2 a puffer mögé **őrszemeket** tesz, így nem a visszatérési értéken múlik:
az első változata azért ment át, mert fájl híján a **hiányzó fájl** miatt adott
hamisat, nem a határellenőrzés miatt (mutációval derült ki). Az `EvFileHeader`
mellett `static_assert(sizeof(EvFileHeader) == 20)` áll: a teszt egyszer 12
bájtosra deklarálta újra, és csak az ASan vette észre, hogy a valódi kód
túlírja a teszt pufferét.
