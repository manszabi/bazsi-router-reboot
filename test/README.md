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
> mind a 295 forgatókönyvet).

**A fordítási modell megegyezik az Arduinóéval**: a `bazsi_router_reboot.ino`
és a hét modul (`secret.cpp`, `strutil.cpp`, `netprobe.cpp`, `sync.cpp`,
`eventlog.cpp`, `configstore.cpp`, `webportal.cpp`) **külön fordítási
egységek**, amiket a linker fűz össze – pontosan úgy, ahogy az Arduino teszi a
sketch mappa `.cpp` fájljaival. Így egy hiányzó deklaráció vagy egy nem
található szimbólum már itt kiderül, nem csak az eszközön.

> Új modul hozzáadásakor a `Makefile` **`MODULES`** listáját kell bővíteni. Ha
> kimarad, a link azonnal `undefined reference`-szel bukik – a felejtés nem
> tud csendben maradni.

A suite **kétszer** fut: az ESP32 Arduino 3.3.11 által használt **IDF 5**-ös
ágon, és az **IDF 6**-os ágon is – a deep sleep gomb-ébresztés API-ját ugyanis
az IDF 6 átnevezte, és a sketch mindkét nevet kezeli.

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

A `covgate` küszöbe a `COV_MIN` (alapértelmezés 97,0%). A mérést a
`covreport.py` végzi: a gcov fájlonként számol, tehát a „legelső százalék"
több modul mellett már nem a program lefedettsége lenne.

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

**295 forgatókönyv, 1695 ellenőrzés. Sorlefedettség: 98,65%.**

### A watchdog-etetés globális invariánsa

A program fő állapotgépe nem blokkoló, de a hálózati műveletek **percekig
futhatnak egyetlen `loop()` iteráción belül** – és ez csak azért biztonságos,
mert azokban a ciklusokban kézzel etetünk. Ezt korábban semmi nem kényszerítette
ki, csak a fegyelem: egy új blokkoló ág, amiből kimarad a `feedWatchdog()`,
watchdog-resetet ad, és a hiba csak az eszközön derülne ki.

Ezért **minden forgatókönyv** méri a leghosszabb etetés nélküli szakaszt, és a
harness a végén ellenőrzi. Küszöb: **45 000 ms** – a 90 000 ms-os watchdog fele.
A mérhető legrosszabb valós eset a halott DNS: **33 010 ms** (`WD13`, `R7`,
`BTN2`).

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

Mutációval igazolva: a `FAILURE_STATE` reset-ciklusából kivett `feedWatchdog()`
**31**, a `routerResetAndRetry()`-ből kivett **8** forgatókönyvet buktat meg.
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
globális szabállyá: a hosszú forgatókönyvek jogosan lépik túl (az `R8` 33 kör
alatt 99 bejegyzést ír a 32 elemű gyűrűbe), és az alvás a harness-ben azonnali,
ami a percre vetített rátát torzítja. Ott a célzott `LOG*` és `SER*` tesztek a
helyes eszköz – egy rossz globális küszöb csak zajt adna.

| | |
|---|---|
| `W1`–`W9` | Wi-Fi: konfig portál, DHCP vs. statikus IP, DNS, timeout, újracsatlakozás, netif-elvesztés |
| `S1`–`S6` | Deep sleep: beragadt gomb, kimenetek állapota alvás előtt, ébresztőforrások, RTC-láb megkötés, **a relé láb (`D10` = GPIO10) hold-ja alvás alatt és a feloldása ébredéskor** |
| `RL1`–`RL2` | Relé: a 90 másodperces reset pulzus hossza, elalvás N reset után |
| `R6`–`R8` | **A dokumentált idők mérése**: egy újrapróbálkozási kör ébren töltött 25,5 perce, a felismerési idő 123 mp (élő DNS) / 213 mp (halott DNS), és mind a 33 kör végigjátszása (46,0 óra türelem) |
| `H1`–`H9` | HTTP teszt: CR/LF vágás, hibás státusz, captive portal, Content-Length nélküli válasz, beragadt szerver, **204-es ellenőrzés**, az eszkaláció végpont-sorrendje, **befagyott router-DNS** |
| `CH1`–`CH5` | **Chunked keretezés**: egy és több darab, darab-kiterjesztés, nagybetűs hexa, szabálytalan és túlcsorduló méret, 50 kB-os darabfolyam |
| `PG1`–`PG2` | Ping (a gateway-ellenőrzéshez és a várakozások korai lezárásához): 2-a-4-ből szabály és korai kilépés |
| `C1` | Konfig fájlok írása/olvasása, csonkítás, hiányzó fájl |
| `B1` | Gomb debounce: zajtüske vs. tartós nyomás |
| `LAT1`–`LAT7` | **Megszakítás-alapú gombretesz**: blokkoló szakaszba eső rövid nyomás sem vész el; a retesz nem kerüli meg a debounce-t (zajtüske); a wifireset is reteszel; fájlírás alatt a nyomás késik, de megmarad; beragadt gomb nem reteszel; **foglalt zárnál a retesz megmarad**; alvás előtt a megszakítások leválasztva |
| `BTN1`–`BTN3` | **A gombok mintavételezése hosszú várakozás alatt**: a saját ciklusaink 10 ms-onként nézik mindkettőt; a blokkoló `http.GET()` alatti **pollozási** vak ablak mérve (33 010 ms halott DNS-nél, egy kérésnyi) – ezt a `LAT*` retesz hidalja át; a végig nyomva tartott gomb az ablak után is hat |
| `F1`–`F4` | Webszerver: a **programba fordított** beállító űrlap üres fájlrendszer mellett is, **a LittleFS-ről semmit nem szolgál ki** (akkor sem, ha ott vannak a régi fájlok), 404, AP-határidő kitolása |
| `WDT1`–`WDT8` | Watchdog: konfiguráció, etetés a hosszú blokkolások alatt, `delay()` vs. CPU-pörgetés, a feliratkozás tényleges ellenőrzése |
| `SN1`–`SN2` | Biztonsági háló: ha a gomb-ébresztés armolása hibázik, időzítő |
| `SH1`–`SH3` | **Alvás és újraindulás**: a leállás nem csak megvárja a fájlírást, hanem **meg is szerzi a zárat** (az utolsó pillanatban érkező mentés 503-at kap, nem csonka fájlt) – a határidős kilépés mellett; az öt alvási út előfeltételei egymás mellett (időzítő csak oda, ahol a hiba magától elmúlhat; gombébresztés mindenhova a beragadt gombot kivéve); és hogy a **felébresztő gombnyomást nem nézzük beragadt gombnak** (a küszöb mérve) |
| `PWR1`–`PWR4` | **Áramszünet és kézi router-áramtalanítás**: 3 perces router-boot alatt az ESP hozzá sem nyúl a reléhez és 3:21-kor kezd tesztelni; ha a router egyáltalán nem áll fel, az első reset 12 perckor; üzem közbeni kézi áramtalanításnál a türelmi idő **2,0 perc**, és a 90 mp-en belül visszadugott routernél nincs relé-impulzus |
| `BNC1`–`BNC4` | **Gombpattogás**: a pattogó nyomás egyszer reteszel és nem hagy hátra „félig lenyomott" állapotot (különben egy későbbi magányos felfutó él spontán újraindítást okozna); a pattogó tüske nem kerüli meg a debounce-t; a folyamatosan recsegő, kopott gomb **nem** indít újra; a beragadt-gomb ellenőrzés egyetlen beolvasásból dönt |
| `SER6`–`SER7` | **A soros port életciklusa**: 115200 baud, az első sor csak a CDC beállása után megy ki, alvás előtt `flush` majd `end` **a legvégén**, és a lezárás után egyetlen sort sem írunk – a beragadt gomb ágát is beleértve |
| `SER8`–`SER10` | **Ellenséges soros terhelés**: a küszöb körül ingadozó heap (15 sor/perc), a mindig bukó naplómentés tartós kiesés alatt (10 sor/perc), és a 33 körös kétnapos létra – mind a 30 sor/perces költségvetés alatt |
| `HP10` | **A `LOW HEAP` bejegyzés spam-védelme**: a küszöb körüli ingadozás védelem nélkül 30 perc alatt **32 bejegyzést** írt (a teljes körpuffert kisöpörve), a `lastEventWas()` után **1** |
| `AP1`–`AP4` | **AP portál űrlap**: az üres címmező törlést jelent (a DHCP-re váltás útja); az előkitöltés **soha nem tartalmazza a jelszót**; az SSID **HTML-escape-elve** kerül a lapra (XSS ellen); az előkitöltéssel a statikus IP megmarad jelszócserénél |
| `LOG1`–`LOG3` | **Naplóoldal**: emberi olvasásra készül (szöveges reset ok, nap/óra/perc/mp uptime, `Param` jelmagyarázat, ami nem ütközik a táblázatcella-mintával); **semmilyen konfigurációs érték nem jelenik meg**; a körpuffer körbefordulása után is pontosan 32 sor |
| `LOG4`–`LOG5` | **A napló írása**: pislákoló Wi-Fi mellett egy `WIFI LOST` sorozatból csak az első kerül be (ugyanaz a szabály, mint a `TEST FAIL`-nél), a soros port sem árad meg; a naplózás **nem nyúl a fájlrendszerhez** (nem hoz létre fájlt, üres tárral is működik) és **nem ágyaz kritikus szakaszt kritikus szakaszba** |
| `LOG6` | **A napló túlcsordulása**: az írási pozíció körbefordul a 32 biten, de a méret kettő hatványa, ezért az index folytonos marad (a 40 írásból mind a 32 legutóbbi megvan, egyik sem kétszer); a `/log` a fordulás után is ép, és egyik olvasó sem indexel ki |
| `FS11`–`FS14` | **Fájlkezelés hibainjektálással**: sorvégek (CRLF) és csupa-whitespace tartalom, bináris szemét és beágyazott NUL, a puffernél hosszabb fájl (csonkolás túlcsordulás nélkül), menet közben megtelő fájlrendszer (a **zár felszabadul**, a gombok tovább működnek), és a félbeszakadt mentés rögzített viselkedése |
| `FS1`–`FS10` | LittleFS hibák: csatolás, írásvédettség, megtelt tár, **néma írási hiba**, csonka olvasás, törlés tartalék útvonala |
| `FT1`–`FT8` | Végzetes hiba: betölthetetlen konfig vs. „nincs még konfig", LED-villogás, gombok |
| `WF1`–`WF6` | Csatlakozási hiba: újrapróbálkozás vs. AP portál, RTC-számláló |
| `WD1`–`WD13` | Ismétlődő watchdog újraindulás; a leghosszabb etetés nélküli szakasz minden üzemmódban, a **halott DNS** legrosszabb esetét is beleértve |
| `WDT14`–`WDT15` | **Az „1 óra hibátlan működés” mihez képest mér**: a mostani indulás kezdetéhez, nem abszolút `millis()` értékhez – nagy kezdő `millis()` mellett és a **körbefordulás** átlépésekor is (mindkét mérés elbukik, ha valaki visszanézi abszolút alakra) |
| `HP1`–`HP9` | **Heap felügyelet**: az állapotsor félóránként megy ki (nem körönként), a figyelmeztetés csak a küszöb átlépésekor; egyetlen mélypont **nem** indít újra, tartós kritikus szint igen; a router reset számláló **túléli** az újraindulást és pontosan egyszer használódik fel; AP módban, relé-impulzus és fájlírás közben nincs újraindulás; három sikertelen kör után `MODE_FATAL`, nem boot loop; és egy valódi lassú szivárgás végponttól végpontig |
| `GWH1`–`GWH5` | **Mi marad meg egy heap-újraindulás után**: a router reset utáni **ellenőrző ablakban** nincs önkéntes újraindulás (a gateway-eszkaláció kétfázisú, és a fázisokat összekötő állapot nem vinne át) – mutációval ellenőrizve; az ablak végén a döntés rendesen megszületik (AP mód a helyes indoklással); a teljes leltár arról, mi vész el, és miért olcsó újraszámolni; és a **2 napos ablak** számlálója (`RTC_DATA_ATTR`, tehát szoftveres resetre nullázódna) szintén átmegy – de csak a saját újraindulásunkon, gombnyomásnál marad a tiszta lap |
| `NV1`–`NV11` | **A napló mentése a fájlrendszerre**: kimegy a három fontos pillanatban (és a relé kapcsolása **előtt**); az írás sikerességét visszaolvasás ellenőrzi, a néma írási hibát is elkapva; a mentés atomikusan szerzi meg a zárat és fel is oldja, foglalt zárnál kimarad; mentés közben nincs gombos újraindulás és másik írás; a lap a **frissebbet** tölti be (élő RTC → RTC, áramszünet után → fájl); hiányzó, üres, csonka, rossz magic-ű és hazudós fejlécű fájl **egyike sem okoz gondot**, mindkettő üresnél nincs napló a lapon; NTP időbélyeg; a félúton bukó betöltés nem hagy kevert puffert; **mind a négy alvás** menti a naplót (nem csak a két időzített), és csatolatlan fájlrendszernél magától kimarad; végül az **óraszinkron minden kapcsolati úton elindul** – a korai kilépéseken is, ahol az `initWiFi()` nem fut le; és **sikertelen NTP mellett** a teljes eszkaláció végigfut, a `/log` a darabszám-alapú szabályra esik vissza, `-` áll az Idő oszlopban, és sehol nincs hamis 1970-es dátum |
| `NV12`–`NV16` | **A naplómentés hibaútjai**: megtelt fájlrendszer (rövid írás), a **flash kímélése** (változás nélkül nem írunk újra), a „mentve: …" időbélyeg, és az olvashatatlan fájlrendszer három ága – írás után lehetetlen visszaolvasás, megnyithatatlan naplófájl a lapon, félbeszakadó fejléc-átugrás |
| `NV15`, `AP5`–`AP7`, `WF11` | **A lefedettség-mérésből előkerült vakfoltok**: mind a **10 indulási ok** emberi neve a `/log` oldalon (plusz a nem nevesített `ESP_RST_USB`); **aposztróf** és társai az SSID-ben (a HTML-escape ezen ága sosem futott le); mentés csatolatlan fájlrendszerrel → tiszta 500; a „router reset után sem jött vissza a WiFi" hibaág; és hogy **mind a négy mező egyformán tűri** a beillesztett szóközöket (a határértékes érték záró szóközzel is mentődik, a túlméretes viszont továbbra is hiba) |
| `LOG7`–`LOG9` | **A naplózás változásainak utóhatásai**: a megnőtt `/log` oldal (Idő oszlop, Forrás sor, két új eseménykód) a legrosszabb esetben – tele körpuffer, valós időbélyegekkel – is **4546 bájt**, tehát belefér a stream 6144 bájtos kezdő pufferébe; egy soros porton keresztüli firmware frissítés után a **régi elrendezésű** RTC napló érvénytelen (a magic egyben verziójelző); és a `wifireset` a naplófájlt szándékosan **nem** törli |
| `CC1`–`CC3` | **Két task, egy memória**: a portál futása alatt a `loop` egyetlen `WiFi.begin()`-t sem ad ki (tehát a konfigurációs puffereket nem olvassa), miközben mentések érkeznek; ugyanez a `MODE_FATAL` ágon, ahol a webszerver **még fut**; és két mentés között a négy puffer mindig **együtt** vált át |
| `E1`–`E6` | Végponttól végpontig: egészséges ciklus, router reset, AP mód, gombok, visszatérő WiFi |
| `P1`–`P20` | Beállító portál mentése: **két fázisú commit** (hibás mezőnél semmi sem íródik), validáció, írási hiba, jelszó-szivárgás, határidő, IP+gateway páros, whitespace (az IP/gateway vágását is beleértve), mentés közbeni gombnyomás, halasztott újraindítás, **503 zárolt konfignál** |
| `CPU1`–`CPU2` | A loop() nem pörgeti a CPU-t a várakozó állapotokban |
| `X1`–`X14` | Határesetek: gomb a relé pulzus közben, nyílt hálózat, SSID/jelszó határértékek, wifireset törlési sorrend és **sikertelen törlés → végzetes hiba**, query-paraméter, 1 napnál hosszabb uptime |
| `PO1`–`PO3` | Áramszünet: mekkora router-indulási késést tolerál |
| `R1`–`R4` | Újrapróbálkozási politika: rossz jelszó vs. hiányzó hálózat, 2 napos határ |
| `L1`–`L7` | Diagnosztikai napló: rögzítés, /log oldal, körpuffer, spam-védelem, esemény-kódok, **mind a 12 címke**, üres napló |
| `SB1`–`SB5` | Beragadt gomb: mindkét gomb, váltakozó LED-villogás, naplózás, **az ismétlődő kör spam-védelme** |
| `SER1`–`SER5` | Soros kimenet: terhelés (nem árasztja el a konzolt), **1-alapú teszt sorszám**, a hibaszámláló a teszt **után**, sikernél csak `Successful Test`, eltérésnél a kapott törzs is |
| `OV1` | Számlálók korlátosak több reset cikluson át |
| `IP1`–`IP3` | Csak IPv4 fogadható el (IPv6 és `0.0.0.0` nem) |
| `LED4`–`LED6` | **LED-ek őszintesége**: a Wi-Fi LED legfeljebb egy `SUCCESS_DELAY`-ig késhet (mérve: 60 mp), a státusz LED normál üzemben végig világít, AP módban a mentés utáni türelmi idő alatt is villog |
| `WDT8b` | **A LittleFS formázása** is belefér a watchdog ablakába: 7,5 mp tipikus és 51,5 mp rossz eset, mindkettő a 90 mp alatt |
| `RR1`–`RR3` | **A router újraindításának üteme**: befagyott DNS-nél 4 reset, köztük 4 p 34 mp; teljes kiesésnél 4 reset, köztük 13 p 38 mp (a 10 perces bootvárakozás érintetlen); egyetlen sikeres teszt nullázza a reset-számlálót |
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

A `make cov` tíz sort jelez, mindegyik védekező vagy bizonyíthatóan
elérhetetlen – ezeket **nem** kell tesztelni:

| Hol | Miért nem érhető el |
|---|---|
| `eventName()` `default` ága | mind a **14** eseménykódnak van címkéje – az `L6` mindet ellenőrzi. Ez a lista **kétszer is elcsúszott** (előbb a `GW UNREACH`, majd a `LOW HEAP` / `HEAP RESTART` maradt ki), ezért a teszt kommentje most kimondja: új eseménykódnál ezt is bővíteni kell |
| A maradék ~1,4% | mind **megjelölve** a forrásban: védelmi ágak (`eventName` `default`, `localtime_r` hibája, `encodeSecret` túlcsordulása – ez utóbbi szerkezetileg elérhetetlen, mert 3 + 2×63 = 129 = a puffer mérete), és olyan sorok, ahová csak a **két task közötti valódi verseny** vezet (a gombkezelők foglalt-zár visszatérései). A `break` a `wifiGiveUp()` után azért fehér, mert a harness az elalvást kivétellel modellezi |
| `readConfigValue()` `file.close()` a könyvtár-ágon | a stub nem tud könyvtárat adni |
| `readChunked()` záró `return n`-je | csak akkor érhető el, ha a záró CRLF olvasása közben szakad meg a stream – a `CH*` esetek a többi ágon lépnek ki |
| `testInternetPing()` záró `return`-je | a ciklus minden ága korábban visszatér (az utolsó körben `remaining == 0`) |
| `gatewayUnreachable()` cím-értelmezési ága | a `staticConfigActive` csak akkor igaz, ha az `initWiFi()` már sikeresen értelmezte a gateway-t, és azt csak a POST kezelő írja át – az viszont újraindít |
| POST `!fsReady` ága | ha a LittleFS nem csatolható, a `setup()` `MODE_FATAL`-ba megy, és a portál el sem indul |
| A jelszókódolás puffer-hiba ága (3 sor) | a POST már ellenőrzi a hosszt, a puffer pedig pontosan 63 karakterre méretezett – nem tud elbukni |
| A wifireset zár-ütközési `return`-je | a `savingConfig` kapu és a zárszerzés közé egyszálú futásban nem ékelődhet másik író – ez pont a valódi kéttaskos versenyhelyzet védőága |
| `FAILURE_STATE` záró `break`-je | előtte `wifiGiveUp()` deep sleepbe vagy AP módba visz |

## Fontos

A stubok **nem** emulálják az ESP32-t. Azt ellenőrzik, hogy a firmware
vezérlési logikája, időzítései és API-használata helyes-e. A valódi rádió,
lwIP, LittleFS és a relé viselkedését hardveren kell igazolni.

## Veletlen allapotgep-bejaras (PROP1-PROP8)

A tobbi 295 forgatokonyv **kezzel irt**: pontosan azokat az utakat jarja be,
amikre a szerzo gondolt. A PROP forgatokonyvek forditva dolgoznak - veletlen
kornyezeti esemenysorozatot generalnak (WiFi-szakadas, gombnyomas tetszoleges
pillanatban, HTTP-valasz, heap-eses, fajlrendszer-hiba), es nem azt allitjak,
hogy MI TORTENJEN, hanem hogy mi **nem tortenhet soha**:

| | Tulajdonsag |
|---|---|
| P1 | a rele sosem marad behuzva a pulzusnal tovabb (a router aram nelkul) |
| P2 | a `cycleIndex` sosem lep ki a `TEST_ENDPOINTS[]` tablabol |
| P3 | `resetEvents` / `rtcRetryRounds` a sajat korlatjuk alatt |
| P4 | az esemenynaploban csak ervenyes esemenykod all |
| P5 | a nyilt szovegu jelszo soha nincs egyetlen fajlban sem |
| P6 | a nyilt szovegu jelszo soha nem kerul a soros kimenetre |
| P7 | a `MODE_CONFIG` / `MODE_FATAL` terminalis (csak ujraindulassal van kiut) |

**Determinisztikus**: a magok fixek, a CI-ban mindig ugyanaz a nyolc bejaras fut.
Egy veletlenszeruen bukdacsolo teszt hasznalhatatlan lenne. Helyben tovabb lehet
keresni ugyanezzel a keszlettel:

```
RNDSEED=12345 RNDSTEPS=2000000 build/run-idf5 PROP
```

**Az ELERES is merve van** (`[eleres]` sor), es kovetelve: minden bejarasnak el
kell jutnia a program valamelyik erdemi againak. Ez nem oncel - a fejlesztes
kozben **ketszer** fordult elo, hogy egy tulajdonsag URESEN ment at:

1. Az elso valtozat a `loop()` hivasok **kozott** mintavetelezte a rele labat -
   es igy sosem latta HIGH-on, mert a 90 mp-es pulzus egyetlen `loop()`
   iteracion belul zajlik le. A meres ezert a stubba kerult (`g_relayMaxHighMs`),
   ugyanoda, ahova a watchdog-rese.
2. Ket tulajdonsag maga volt hibas: az `rtcEvNext` **monoton szamlalo**, nem
   gyuruindex, a `terminalSeen` jelzot pedig nem nullazta az ujraindulas.
   Nyolcbol nyolc, illetve nyolcbol ot bejaras "bukott" - egyik sem a program
   miatt.

Mutacioval igazolva, mind a negy a **helyes** tulajdonsagot buktatja:

| Mutacio | Elkapja |
|---|---|
| `RESET_PULSE` 90 -> 200 mp | P1, 8/8 bejaras |
| `MAX_CYCLE_INDEX` 4 -> 5 | P2, 8/8 |
| a jelszo kiirasa a soros portra | P6, 8/8 |
| a jelszo kiirasa egy fajlba | P5, 8/8 |

## Fuzzing: `make fuzz` (clang libFuzzer)

Minden mas teszt - a 295 kezzel irt forgatokonyv es a 8 veletlen bejaras is -
**ervenyes vagy legalabbis elkepzelt** bemenetekkel dolgozik. A fuzzer nem:
kifejezetten olyan bajtsorozatokat keres, amikre senki nem gondolt, es a
lefedettseg alapjan tanul, merre erdemes menni.

| Celpont | Mit elemez | Ki irja a bemenetet |
|---|---|---|
| `fuzz-post` | a POST urlap-elemzo | **barki, aki AP modban csatlakozik** |
| `fuzz-secret` | `decodeSecretInPlace()` / `encodeSecret()` | a `/pass.txt` (serult vagy atirt) |
| `fuzz-evlog` | az `/evlog.bin` betolto | a naplofajl (felbeszakadt iras) |
| `fuzz-config` | `readConfigValue()`, `fileMatches()` | a negy konfig fajl |

A **`fuzz-post` a lenyeg**: ez az egyetlen felulet, aminek a tuloldalan
tenyleges tamado ulhet. A masik harom olyan adatot olvas, amit maga az eszkoz
irt ki - ott a "serult flash" a realis fenyegetes, nem a rosszindulat.

Mind a negy **ASan + UBSan alatt** fut: a fuzzer onmagaban csak az
osszeomlast latna, a csendes puffertulcsordulast nem.

```
make fuzz               minden celpont 30 mp
FUZZSEC=600 make fuzz   hosszabban
build/fuzz-post -runs=100000
build/fuzz-post crash-<hash>    egy talalat ujrajatszasa
```

**MUTACIOVAL IGAZOLVA, hogy a negy harness tenyleg eleri a kodot** - egy
fuzz-teszt legveszelyesebb hibaja az, ha nulla talalattal fut, mert nem jut el
sehova:

| Mutacio | Eredmeny |
|---|---|
| a POST-elemzo `candidate` pufferje 16 bajt + `strcpy` | **elkapva** 60 mp alatt (stack-buffer-overflow) |
| az evlog-betoltobol kivéve a sajat `count` hatarellenorzes | **elkapva** 60 mp alatt |
| `decodeSecretInPlace`: tulcsordulas CSAK a `v1:9f` bemenetnel | **elkapva** 50 mp alatt (a fuzzer maga talalta meg a prefixet) |
| `readConfigValue`: tulcsordulas CSAK egy adott pufferméretnél | **elkapva** 50 mp alatt |

Egy otodik mutacio **NEM** bukott el, es ezt is kimondjuk: a `d.ssid` tulirasa
a `ConfigDraft` **strukturan belul** marad, azt pedig az ASan alapertelmezesben
nem latja. Ez a modszer valos hatara, nem a harnesse.

### A meres hatara

Ez **hoston** fut, stub Arduino API-k felett. Amit megfog: puffertulcsordulas,
hataron tuli olvasas, definialatlan viselkedes, vegtelen ciklus - **a sajat
kodunkban**. Amit nem: a valodi ESPAsyncWebServer / lwIP hibait (nem a mi
kodunk, es nem is forognak itt), es az `IPAddress::fromString()`-et sem, mert az
a hoston stub - azt fuzzolni a sajat stubunk teszteles lenne, nem a firmware-e.

## A ket task tenyleges osszefonasa (ILV1-ILV5)

A program **ket taskon** fut: a loop task mer, dont es alszik, az async_tcp
task futtatja a HTTP kezeloket. A ket task kozos allapoton dolgozik
(`savingConfig`, `restartPending`, `restartAt`, `apDeadline`), es epp ezert van
a `portMUX` spinlock. Eddig azt bizonyitottuk, hogy a kritikus szakasz sosem
agyazodik egymasba - azt viszont soha, hogy a ket task **tetszoleges pontokon
valtakozva** is helyesen viselkedik.

**Hol vannak a valodi megszakitasi pontok?** Egyetlen magon a taskvaltas nem
barhol tortenik, hanem ott, ahol az utemezo szohoz jut:

| Horog | Irany | Mit modellez |
|---|---|---|
| `g_onDelay` | loop() → HTTP kezelo | a loop blokkolo szakaszai; az async_tcp magasabb prioritasu (3 vs 1), tehat bevag |
| `g_onFsWrite` | HTTP kezelo → loop() | a flash-iras ideje; ilyenkor az async_tcp **mar a zar birtokaban van**, de meg nem engedte el |

| | Mit rogzit |
|---|---|
| ILV1 | `/ping` a loop **minden** `delay()`-eben (3999 osszefonas) - a keep-alive a versenyben is hat |
| ILV2 | `/log` olvasas a naplo irasa kozben - a gyuru ep marad |
| ILV3 | POST mentes a loop koze ekelve; az injektalas abbahagyasa utan a halasztott ujrainditas **pontosan egyszer** fut le, a zarat megszerezve |
| ILV4 | a loop a **flash-iras kozben** - a lejart AP hatarido ellenere sem alszik el |
| ILV5 | mindket irany egyszerre, veletlen suruseggel |

### Amit az ILV4 megmutatott

Azt akartam merni, hogy a mentes alatt elalvo loop atmegy-e a
`lockConfigBeforeShutdown()`-on. **Nem alszik el, es nem is jut el a zarig**: a
`handleConfigMode()` mar a DONTESNEL kizarja.

```cpp
if (!configWriteInProgress() && !restartRequested() &&
    (int32_t)(currentMillis - apDeadline) >= 0) { apSleep(); }
```

A vedelem tehat **ket retegu**: az elso itt van (a loop ra sem lep az alvas
agara), a masodik a `lockConfigBeforeShutdown()`, ami azokat az utakat fogja
(gomb, watchdog), amik nem ezen a felteteten at jonnek. Az ILV4 az elso reteget
rogziti - es kulon ellenorzi, hogy az ablak tenyleg **nyitva volt** (a zar allt,
a hatarido lejart, a loop tenyleg futott), kulonben az allitas ures lenne.

### Mutacioval igazolva

| Mutacio | Elkapja |
|---|---|
| a `!configWriteInProgress()` feltetel kivéve az alvas-dontesbol | **ILV4** (es a kilepes tenyleg bekovetkezik) |
| a halasztott ujrainditas hatarideje sosem jar le | **ILV3** |
| beagyazott `portENTER_CRITICAL` a naplozasban | **10 ellenorzes** az ILV-kben |

## A stubok igazolasa: `make stubcheck`

**A teljes host-tesztkeszlet ervenyessege ezen all.** Az 1761 ellenorzes mind a
`test/stubs/` alatti modelleken keresztul lat: ha egy stub teved, a teszt
**zold**, az eszkoz meg hibazik - es semmi nem szolna. Ez volt a tesztelesi
piramis egyetlen meg nem vizsgalt alapja.

Mennyire eles ez? A rele a **D10**-en van. Ha a XIAO_ESP32C3 variansaban a D10
szama valaha megvaltozik, a sketch tovabbra is lefordul, minden teszt zold marad
(a teszt is a sajat konstansat hasznalja) - es az eszkoz **egy masik labat**
kapcsolgatna.

Amit osszevet (40 allitas):

| | |
|---|---|
| labkiosztas | D0/D1/D3/D4/D10 a valodi `variants/XIAO_ESP32C3/pins_arduino.h`-bol |
| `wl_status_t` | a modellezett ertekek a valodi `WiFiType.h`-bol |
| `esp_reset_reason_t` | a felsorolas **sorrendje** (az ertekek implicitek, egy beszurt elem mindent elcsusztatna) |
| `esp_task_wdt_config_t` | mezonevek es tipusok, sorrendben |
| `ESP_OK` / `ESP_FAIL` / `ESP_ERR_*` | a modellezett kodok |
| `HTTPClient` | `setTimeout(uint16_t)`, `setConnectTimeout(int32_t)` |
| `ESPAsyncWebServer` | `params()`, `getParam(size_t)`, `name()`/`value()` **referenciat** ad, `isPost()` |
| **viselkedes** | `STA.cpp`: az AUTH_FAIL ag `!first_connect` feltetele - a `WifiSim::authFail` modellje ezen all |
| **viselkedes** | `main.cpp`: a loopTask minden iteracioban etet, ha fel van iratkozva - a `coreLoopStep()` ezt modellezi |

Az utolso ketto azert van benne, mert a **forraskod kommentjei nev szerint
hivatkoznak rajuk**. Ha azok elmozdulnak, a rajuk epulo indoklas is ervenyet
veszti.

A CI a **firmware-build** jobban futtatja: az az egyetlen hely, ahol a pontosan
kituzott verziok a lemezen vannak. Helyben:

```
STUBROOTS="$HOME/.arduino15 $HOME/Arduino/libraries" make stubcheck
```

**Mutacioval igazolva** - hat kulonbozo elcsuszast kap el:

| Mutacio | Elkapja |
|---|---|
| a rele laba 10 → 11 a tesztben | igen |
| `WL_CONNECT_FAILED` 4 → 5 a stubban | igen |
| egy `ESP_RST_` elem kiesik (a tobbi elcsuszik) | igen, a **2.** indextol |
| `bool trigger_panic` → `int` | igen |
| az `isPost()` eltunik a stubbol | igen |
| a **valodi** header formaja valtozik (a minta nem illeszkedik) | igen - "NEM SIKERULT KIOLVASNI", nem csendes atmenet |
| hianyzo valodi forras (rossz gyoker) | igen - "NEM TALALHATO" |

Ezen felul **uresseg-vedelem**: 24 allitas alatt a futas megbukik. Ket
ellenorzo mar volt ebben a projektben, ami azert volt "tiszta", mert nem
vizsgalt semmit; harmadik ne legyen.

## A maradek hibaagak (COV1-COV6)

A lefedettseg 98,71% volt. A hianyzo resz nagy reszet olyan fuggvenyek zaro
kapcsos zarojele adta, amik **sosem ternek vissza normalisan** (alszanak vagy
ujrainditanak) - az nem hianyossag. Maradt viszont nehany **valodi hibaag**,
amit egyetlen forgatokonyv sem erintett. Ezek epp azok, amik akkor futnak le,
amikor mar amugy is baj van.

| | Mit fed le |
|---|---|
| COV1 | `eventName()` ismeretlen kodra (`"?"`) - a naplo tuleli a firmware-frissitest is |
| COV2 | a naplo-betolto **sajat** hatarellenorzese, a hivotol fuggetlenul |
| COV3 | a naplofajl eltunik a fejlec es a bejegyzesek olvasasa **kozott** |
| COV4 | **konyvtar** egy konfig utvonalon |
| COV5 | a wifireset foglalt zarnal nem torol es nem indit ujra |
| COV6 | a gateway-ellenorzes vedelmi aga ertelmezhetetlen cimnel |

A COV4 kedveert a stub megtanult **konyvtarat** modellezni (`g_fsDirs`). Egy
stub, ami egy valos allapotot nem tud eloallitani, csendben csokkenti a
tesztek erejet: az az ag addig lefedhetetlen volt.

**Lefedettseg: 98,71% -> 99,08%**, a kapu kuszobe 97,0% -> 98,8%.

### Ami szandekosan feher marad - es mostantol a FORDITO tartja igy

Nyolc sor maradt, mindegyik **szerkezetileg elerhetetlen**. Eddig ezt csak
kommentek allitottak; harom premissza mostantol `static_assert`:

| Sor | Miert elerhetetlen | Mi tartja |
|---|---|---|
| `webportal` - "a jelszo kodolasa nem fert a pufferbe" (3 sor) | a `ConfigDraft::pass` legfeljebb `PASS_MAX_LEN`, a kodolt alak pedig pontosan ekkora pufferbe megy | **2 `static_assert`** a `webportal.cpp`-ben |
| `netprobe` - a `readChunked()` zaro `return n` | minden nem ures darab legalabb egy bajtot ad, tehat a hasznos-teher korlat mindig elobb ut a darabszam-korlatnal | **`static_assert`** a `netprobe.h`-ban |
| `netprobe` - a ping zaro `return` | 4 probaval es 2-es kuszobbel a ciklus mindig korabban lep ki | komment (a fordito koveteli meg a sort) |
| `eventlog` - `localtime_r` hibaja | az `epoch` `uint32_t`, tehat legfeljebb 2106 - azt minden platform ertelmezi | komment (ha a mezo 64 bitesre no, a kezeles keszen all) |
| `sketch` - ket "csak a ket task kozotti valodi verseny vezet ide" ag | a fenti kapu ugyanazt a jelzot nezi mikromasodpercekkel korabban | komment |

### Amit ez a kor megtanitott

**Ket sajat hibat az ASan talalt meg, a sima build atengedte:**

1. A COV2/COV3 ujradeklaralta az `EvFileHeader`-t - **12 bajtosra a valodi 20
   helyett**. Ket forditasi egyseg, ugyanaz a nev, elteroformatum: az
   egydefinicios szabaly serult, es a valodi kod tulirta a teszt pufferet.
   Mostantol `static_assert(sizeof(EvFileHeader) == 20)` all mellette.
2. A COV6 egy 18 bajtos szoveget masolt a 16 bajtos `gatewayStr`-be.

**Es egy teszt jo eredmenyt kapott rossz uton.** A COV2 elso valtozata csak
azt allitotta, hogy a betolto hamisat ad - de fajl hijan a **hianyzo fajl**
miatt adott hamisat, nem a hatarellenorzes miatt. Mutacioval derult ki: a
felso korlat kivetele utan a teszt **valtozatlanul atment**. Most van ervenyes,
nagy naplofajl, es a puffer moge **orszemek** kerulnek - igy nem a
visszateresi erteken mulik.

Mutacioval igazolva mind a negy: a naplo-betolto felso korlatja, a
konyvtar-ellenorzes, a wifireset zarszerzese es a gateway-cim ellenorzese -
mindegyik kivetele megbuktatja a hozza tartozo forgatokonyvet.
