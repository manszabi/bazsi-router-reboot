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
- **Minden forgatókönyv külön processzben fut** (`fork()`), mert a sketch
  globális állapota (`testState`, `timing`, `uiFlags`) egyébként átszivárogna
  az esetek között. Ez egyben hű is a valósághoz: minden eset hidegindítás.

## Lefedett esetek

**291 forgatókönyv, 1083 ellenőrzés. Sorlefedettség: 98,64%.**

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
