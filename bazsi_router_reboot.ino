#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <ESPping.h>
#include <string.h>
#include <ctype.h>
#include "LittleFS.h"
#include "esp_sleep.h"
#include "esp_idf_version.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_attr.h"
// Az esp_timer_get_time()-ot a core is expliciten includeolja (esp32-hal-misc.c),
// nem hagyatkozik a FreeRTOS fejlecek atteteles behuzasara. Mi sem tesszuk.
#include "esp_timer.h"
#include <time.h>
// A rele labanak rogzitese deep sleep idejere: gpio_hold_en() +
// gpio_deep_sleep_hold_en(). A C3-on a digitalis padek (GPIO6-21) holdjat csak
// ez a paros tartja meg alvas alatt (driver/gpio.h, a gpio_hold_en 3. megj.).
#include "driver/gpio.h"

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char PARAM_SSID[]    = "ssid";
const char PARAM_PASS[]    = "pass";
const char PARAM_IP[]      = "ip";
const char PARAM_GATEWAY[] = "gateway";

// Keep-alive: amíg a lap NYITVA van, 60 mp-enként jelez. Enélkül az AP mód
// "tétlenség" órája a lap betöltésétől ketyegne, nem az utolsó interakciótól -
// és egy lassan begépelt jelszó közben elaludna az eszköz (mérve: 6 perc
// gépelés után a Submit már nem érné el).
// A visibilitychange azért kell, hogy app-váltás után visszatérve azonnal
// frissüljön a határidő, ne csak a következő 60 mp-es ütemnél.
// Mindkét űrlapon és a naplóoldalon ugyanez a szöveg szerepel; a const char[]
// a flash .rodata szekciójába kerül (drom0_0_seg), RAM-ot nem foglal.
// Egyetlen forrás: a fordító fűzi össze a literálokat, futásidőben nem másolunk.
#define KEEPALIVE_JS_LIT \
  "<script>f=()=>fetch('/ping?'+Date.now());setInterval(f,6e4);" \
  "document.onvisibilitychange=()=>document.hidden||f()</script>"
const char KEEPALIVE_JS[] = KEEPALIVE_JS_LIT;

// A beállító űrlap FEJLÉCE és LÁBLÉCE. A középső, mezőket tartalmazó rész
// futásidőben generálódik, mert a mentett SSID-t, IP-t és gateway-t
// ELŐKITÖLTVE mutatjuk - lásd sendConfigForm().
const char FORM_HEAD[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>ESP Wi-Fi Manager</title></head><body>"
  "<h2>ESP Wi-Fi Manager</h2>"
  "<form action=\"/\" method=\"POST\">";
const char FORM_TAIL[] =
  "<small>Statikus IP-hez mindket cimmezot toltsd ki, csak IPv4. "
  "DHCP-hez hagyd mindkettot uresen.</small><br>"
  "<input type=\"submit\" value=\"Submit\"></form>"
  "<p><a href=\"/log\">Diagnosztikai naplo</a></p>"
  KEEPALIVE_JS_LIT
  "</body></html>";

// AP password (WPA2: min. 8, max. 63 karakter)
const char AP_PASSWORD[] = "12345678";
// A WiFi.softAP() visszateresi erteket nem nezzuk, a core pedig rovid jelszonal
// csak annyit tesz, hogy "passphrase too short!" es return false (AP.cpp) - az
// AP letre sem jonne, az eszkoz elerhetetlen lenne, es 5 perc mulva elaludna.
// Ezt fordítási idoben fogjuk meg, hogy egy kesobbi atiras ne tudja elrontani.
static_assert(sizeof(AP_PASSWORD) - 1 >= 8, "AP jelszo: legalabb 8 karakter (WPA2)");
static_assert(sizeof(AP_PASSWORD) - 1 <= 63, "AP jelszo: legfeljebb 63 karakter (WPA2)");

// A HTML űrlapról érkező értékek. Fix méretű bufferek: nincs heap-töredezettség,
// és a szabvány szerinti maximumok egyben validációt is jelentenek.
constexpr size_t SSID_MAX_LEN  = 32;  // IEEE 802.11 SSID
constexpr size_t PASS_MAX_LEN  = 63;  // WPA2-PSK passphrase
constexpr size_t IPSTR_MAX_LEN = 15;  // "255.255.255.255"

char ssid[SSID_MAX_LEN + 1]        = { 0 };
char pass[PASS_MAX_LEN + 1]        = { 0 };
char ipStr[IPSTR_MAX_LEN + 1]      = { 0 };
char gatewayStr[IPSTR_MAX_LEN + 1] = { 0 };

// File paths to save input values permanently
const char ssidPath[] = "/ssid.txt";
const char passPath[] = "/pass.txt";
const char ipPath[] = "/ip.txt";
const char gatewayPath[] = "/gateway.txt";

// Az IPAddress a core 3.x-ben ~28 bájt (16 bájtos unió + típus + zóna + vptr),
// ezért egyik sem globális: mind ott jön létre, ahol használjuk.

// Strapping labak a C3-on: GPIO2, GPIO8, GPIO9 (datasheet, Boot Configurations).
// Set LED GPIO, relay state
constexpr uint8_t ledPin = D4;
// wifi ok led
constexpr uint8_t wifiledPin = D3;
// Set RELAY pin, to router
//
// HARDVER: D10 = GPIO10, KULSO 22k LEHUZO ELLENALLASSAL a GND fele.
// A korabbi D5 = GPIO7 bekotes nem volt stabil. Amit a D10 ad:
//   - nem strapping lab (a C3-on GPIO2, GPIO8, GPIO9 az), tehat a reset
//     alatti szintje a bootot nem befolyasolja;
//   - tovabbra is a digitalis pad tartomanyban (GPIO6-21) van, igy az alvas
//     alatti rogziteshez ugyanaz a gpio_hold_en() + gpio_deep_sleep_hold_en()
//     paros kell, mint eddig (lasd holdRelayForSleep()).
// A 22k lehuzo NEM elhagyhato: a hold a bekapcsolas es a program indulasa
// kozti ablakban meg nem el, addig a pad nagyimpedanciasan lebegne.
constexpr uint8_t relayPin = D10;
// Set reset pin, esp wifireset pin
//
// FIGYELEM (hardver): a D0 = GPIO2 strapping lab! A chip-reset alatti
// mintavetelnel GPIO2=1 kell mind az SPI boothoz, mind a letoltesi modhoz -
// bekapcsolas/reset KOZBEN nyomva tartott (vagy beragadt) wifireset gombnal
// az eszkoz el sem indul. Ez ellen szoftver nem vedhet: a handleStuckButton()
// csak a mar elindult programbol mukodik, es a beragadt gomb 60 mp-es
// alvas-ebredes kore is chip-resettel ebred, azaz ujra a lenyomott gombot
// mintavetelezne. Uj hardver reviziora a gombot szabad, nem-strapping labra
// erdemes tenni (pl. D2 = GPIO4, ami raadasul RTC-kepes is); a jelenlegi
// bekotesnel a szabaly annyi: bekapcsolas kozben ne legyen nyomva.
constexpr uint8_t wifiresetPin = D0;
// Set reset pin, esp reset/wakeup pin
constexpr uint8_t resetPin = D1;

// Timer variables
// interval to wait for Wi-Fi connection (milliseconds)
constexpr uint32_t interval = 20 * 1000;
constexpr uint32_t SUCCESS_DELAY = 1 * 60 * 1000;
// Szünet KÉT BUKOTT internetteszt között (az eszkaláció üteme). Ne keverd az
// ONLINE_PROBE_INTERVAL_MS-sel: az a hosszú VÁRAKOZÁSOK korai lezárásának
// üteme, és teljesen más állapotokban fut.
constexpr uint32_t PROBE_DELAY = 12 * 1000;
constexpr uint32_t RESET_DELAY = 10 * 60 * 1000;
constexpr uint32_t RESET_PULSE = 90 * 1000;
constexpr uint32_t firstStartDelay = 10 * 60 * 1000;
// A reset ESEMÉNY számláló határa. A számláló még a reset ELŐTT nő, ezért
// 4 tényleges router újraindítás után következik az egy órás alvás.
constexpr uint8_t maxfailureEvents = 5;
// Egységes Wi-Fi újrapróbálkozási politika MINDEN ágon: 3 próba, köztük 30 mp.
constexpr uint8_t wifi_maxRetries = 3;
constexpr uint32_t wifiInterval = 30 * 1000;
// Az AP beállító mód ennyi tétlenség után elalszik (mentés nélkül). Minden
// beérkező kérés újraindítja a visszaszámlálást.
constexpr uint32_t AP_TIMEOUT_MS = 5 * 60 * 1000;

// Meddig próbálkozzunk, ha a hálózat egyszerűen nincs ott? Egy kör:
//    10,0 perc  firstStartDelay várakozás
//   + 2,0 perc  3 csatlakozási próba (3 x 20 mp timeout + 2 x 30 mp szünet)
//   + 1,5 perc  router áramtalanítás (RESET_PULSE)
//  + 10,0 perc  várakozás a router bootolására (RESET_DELAY)
//   + 2,0 perc  újabb 3 csatlakozási próba
//  + 60,0 perc  deep sleep
//  = 85,5 perc  (de csak akkor, ha a kör alvással ZÁRUL)
// Az UTOLSO kör nem alszik: a wifiGiveUp() előbb növel, aztán ellenőriz, tehát
// a 33.-nál már AP módba megy. A tényleges türelem ezért
//   33 x 25,5 + 32 x 60 = 2761,5 perc = 46,0 óra,
// nem 33 x 85,5. Két napon belül marad, sőt tartalékkal: 34 kör is csak
// 47,5 óra lenne. Ha ennyi idő alatt sem jön vissza a net, az már nem az
// eszköz dolga. Mérve: R8.
constexpr uint32_t MAX_RETRY_ROUNDS = 33;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
// Gombok mintavételi köze. 10 ms bőven elég az 50 ms-os debounce-hoz, viszont
// delay()-jel várunk, nem yield()-del, így a CPU nem pörög üresen.
constexpr uint32_t BUTTON_POLL_MS = 10;
// Végzetes hiba jelzése: mindkét LED együtt, gyorsan villog (5 Hz).
constexpr uint32_t FATAL_BLINK_MS = 100;
// Ennyi hibajelzés után az ESP elalszik. Időzített ébresztés NÉLKÜL: csak a
// reset gomb vagy az áramtalanítás hozza vissza.
constexpr uint32_t FATAL_SLEEP_AFTER_MS = 5 * 60 * 1000;
// Beragadt gomb jelzése elalvás előtt: a két LED FELVÁLTVA villog, hogy meg
// lehessen különböztetni a végzetes hibától, ahol EGYÜTT villognak.
constexpr uint32_t STUCK_BLINK_MS = 3000;
// AP beállító mód: a Wi-Fi LED villog 1 Hz-cel, a státusz LED végig VILÁGÍT.
// Lassabb a végzetes hiba 5 Hz-énél, hogy össze ne lehessen téveszteni vele.
constexpr uint32_t AP_BLINK_MS = 500;
// Router reset pulzus: a státusz LED villog 2 Hz-cel, a Wi-Fi LED KI marad
// (a router áram nélkül van, kapcsolat sincs). A két villogó jelzés így a
// villogó LED-ről és a sebességről is megkülönböztethető.
constexpr uint32_t RESET_BLINK_MS = 250;
// Watchdog timeout. Nagyobb kell, mint a leghosszabb olyan blokkolás, amit NEM
// tudunk etetni - ez a http.GET(). A rossz eset NEM a szerver hallgatasa
// (5 mp connect + 10 mp valasz = 15 mp), hanem a HALOTT DNS, mert a
// nevfeloldas a connect timeouton KIVUL esik:
//   NetworkClient::connect(host,...) eloszor Network.hostByName()-t hiv, es
//   annak nincs timeout parametere (NetworkClient.cpp:310-315).
//   Egy lwIP DNS lekerdezes szervereenkent ~7 mp alatt adja fel
//   (DNS_MAX_RETRIES=4, DNS_TMR_INTERVAL=1000 ms, a tmr 1,1,2,3 lepesekben nő
//   -> 7 tick), DHCP-tol jellemzoen 2 szerver jon.
//   Ha van globalis IPv6 cim, a hostByName KETSZER kerdez (eloszor csak
//   AF_INET6-ot, aztan AF_UNSPEC-et; NetworkManager.cpp) -> 2 x 14 mp.
//   A lwip_getaddrinfo hibaja pozitiv EAI_* kod (netdb.h: 200-210), amit a
//   NetworkClient::connect igazkent lat, ezert meg egy 0.0.0.0-ra iranyulo
//   connect is lefut a maga 5 mp-evel.
// Realis legrosszabb eset: 2 x (2 x 7) + 5 = 33 mp. A 90 mp igy ~2,7-szeres
// tartalekot ad. (Meresi keretben: WD13.)
constexpr uint32_t WDT_TIMEOUT_MS = 90 * 1000;
// Ennyi watchdog/panic miatti újraindulás után az eszközt instabilnak
// tekintjük, és ugyanúgy leállunk, mint a többi végzetes hibánál.
constexpr uint32_t MAX_WDT_RESETS = 3;
// Ennyi ideig tartó hibátlan működés után a számláló nullázódik.
constexpr uint32_t WDT_COUNTER_CLEAR_MS = 60 * 60 * 1000;
constexpr uint32_t RESTART_GRACE_MS = 2000;  // válasz kiküldése újraindítás előtt

constexpr uint64_t SLEEP_DURATION_US = 3600ULL * 1000000ULL;      // 1 óra
constexpr uint64_t STUCK_BUTTON_SLEEP_US = 60ULL * 1000000ULL;    // 60 másodperc

// Ennyi ideig várunk a beragadtnak látszó gomb ELENGEDÉSÉRE, mielőtt tényleg
// beragadásnak minősítenénk.
//
// MIÉRT KELL EZ? A reset gomb LOW SZINTRE ébreszt az alvásból, tehát a
// felhasználó szükségképpen MÉG NYOMVA TARTJA, amikor az eszköz bootolni kezd.
// Minden gombos ébredés ilyen. Ha a setup() egyetlen pillanatnyi digitalRead()
// alapján döntene, egy teljesen normális, fél-egy másodperces gombnyomás
// "beragadásnak" minősülne, és az eszköz azonnal visszaaludna 60 mp-re - a
// felhasználó szemszögéből "a gomb nem csinál semmit", pedig épp ő nyomta meg.
//
// Mérve (SH3): a döntés régen a Serial.begin() utáni várakozás hosszától
// függött, ami viszont attól, hogy VAN-E USB GAZDA. Bedugott USB-vel a határ
// ~600 ms volt (egy szándékos gombnyomás bőven fölé megy), USB nélkül ~3,5 mp.
// Vagyis ugyanaz a gombnyomás máshogy sült el aszerint, hogy be van-e dugva a
// kábel. Ez a konstans a küszöböt kimondottá és a kábeltől függetlenné teszi.
//
// A hosszát nem kell finomhangolni: egy TÉNYLEG beragadt gomb sosem enged el,
// tehát a 3 mp csak annyit késleltet, ami után úgyis a 60 mp-es alvás jön.
constexpr uint32_t STUCK_BUTTON_CONFIRM_MS = 3000;

// --- Heap felugyelet -------------------------------------------------------
//
// MIERT KELL? A sketch maga semmit nem allokal dinamikusan, az
// ESPAsyncWebServer / AsyncTCP / a Wi-Fi driver viszont igen. Egy lassu
// szivargas vagy elaprozodas honapokig eszrevetlen marad, aztan egy nap az
// eszkoz "csak ugy" nem mukodik - allokacios hiba eseten a legtobb konyvtar
// csendben elbukik, nem panikol. Ezert merunk, kiirunk, es vegso esetben
// magunktol ujraindulunk, MIELOTT barmi elromlana.
// --- Valos ido (NTP) -------------------------------------------------------
//
// MIERT KELL? A naplo eddig csak uptime belyeget hordozott, ami minden
// indulaskor nullarol kezd - ket bootolas esemenyei igy nem rendezhetok
// egymashoz. A LittleFS-re mentett naplo ertelmezesehez viszont epp ez kell:
// melyik a frissebb, a fajlban levo vagy az RTC-ben levo?
//
// A szinkronizalas NEM BLOKKOL: a configTzTime() csak elinditja az SNTP
// klienst, a valasz a hatterben erkezik. Amig nem erkezett meg, az epoch mezo
// 0 marad - a naplo attol meg mukodik, csak uptime-ot mutat.
constexpr char NTP_SERVER[] = "hu.pool.ntp.org";
// Magyarorszag: CET/CEST, a nyari idoszamitas valtasaival egyutt (POSIX TZ).
constexpr char NTP_TZ[] = "CET-1CEST,M3.5.0,M10.5.0/3";
// Ennel korabbi ertek nem lehet valodi szinkron (2025-01-01). A rendszerora
// szinkron nelkul 1970-bol indul, tehat egy egyszeru also korlat elegendo.
constexpr uint32_t NTP_MIN_VALID_EPOCH = 1735689600UL;

constexpr uint32_t HEAP_CHECK_INTERVAL_MS = 10 * 1000;        // mintaveteli koz
constexpr uint32_t HEAP_LOG_INTERVAL_MS   = 30 * 60 * 1000;   // rendszeres sor

// A KET KUSZOB. A C3-on Wi-Fi + webszerver mellett a tipikus szabad heap
// 120-200 KB. A Wi-Fi driver es az AsyncTCP egyszerre tobb KB-os tomboket is
// ker, ezert nem a nulla a veszelyes hatar, hanem az a szint, ahol egy
// szokasos foglalas mar elbukhat.
constexpr uint32_t HEAP_WARN_BYTES  = 25000;   // figyelmeztetes (naplo + soros)
constexpr uint32_t HEAP_CRIT_BYTES  = 12000;   // ujrainditas

// ELAPROZODAS. Lehet 30 KB szabad heap ugy, hogy a legnagyobb OSSZEFUGGO tomb
// csak 4 KB - ilyenkor a szabad heap onmagaban megnyugtato, kozben a
// foglalasok mar buknak. Ezert a legnagyobb tombot kulon is nezzuk.
constexpr uint32_t HEAP_CRIT_BLOCK_BYTES = 6000;

// A kritikus szintnek KI KELL TARTANIA. Egy pillanatnyi melypont (egy epp
// futo HTTP keres, egy nagyobb valasz osszeallitasa) nem szivargas. Harom
// egymas utani minta = 30 masodperc.
constexpr uint8_t HEAP_CRIT_SAMPLES = 3;

// Es a heap miatti ujrainditasnak is van felso hatara: ha az ujraindulas sem
// segit (tartos elaprozodas, valodi szivargas mar indulaskor), a boot loop
// rosszabb, mint a megallas. Ugyanaz a politika, mint a watchdognal.
constexpr uint32_t MAX_HEAP_RESTARTS = 3;

// A hosszú várakozások (firstStartDelay, RESET_DELAY) korai lezárása.
//
// Mindkét várakozás arra való, hogy a router bootolására időt hagyjunk - de ha
// a router hamarabb feláll, felesleges a maradékot kivárni. Ennyi időnként
// megnézzük, hogy visszajött-e a hálózat ÉS az internet; ha mindkettő megvan,
// a várakozás azonnal véget ér. A próba ISMÉTLŐDIK, tehát elég, ha a kapcsolat
// a várakozás BÁRMELY pontján helyreáll: a kilépés az azt követő ütemben
// megtörténik. A teljes időt csak akkor várjuk ki, ha végig nincs meg.
//
// MIÉRT 60 mp? Egy router bootolása 1-3 perc, tehát ennél sűrűbb kérdezés nem
// hozna érdemben korábbi kilépést, viszont rádiózna. Egy 10 perces várakozás
// alatt így legfeljebb 10 próba fut - és mivel a Wi-Fi újracsatlakozás a
// WiFi.persistent(false) mellett NEM ír NVS-t, ez a flasht sem terheli.
constexpr uint32_t ONLINE_PROBE_INTERVAL_MS = 60 * 1000;
// A korai kilépés ping célpontja: fix IP, hogy a névfeloldás ne számítson -
// itt csak azt kérdezzük, van-e út kifelé. (Cloudflare.) Az IPAddress ~28
// bájt és virtuális, ezért itt csak a négy oktett él, a cím a hívás helyén
// jön létre.
constexpr uint8_t PROBE_PING_IP[4] = { 1, 1, 1, 1 };

// Teszt paraméterek
constexpr uint8_t PING_ATTEMPTS = 4;
constexpr uint8_t PING_MIN_SUCCESS = 2;
constexpr uint32_t PING_GAP_MS = 1000;
constexpr size_t HTTP_MAX_PAYLOAD = 96;  // a várt válaszok < 32 bájt
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t HTTP_RESPONSE_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_READ_TIMEOUT_MS = 1500;
// Chunked valasznal hany darabot vagyunk hajlandok vegigolvasni. Minden nem
// lezaro darab legalabb 1 bajtot ad, es a puffer hataran ugyis megallunk, tehat
// ennyi kort elmeletileg sem lehet tullepni - ez csak egy vegso kapaszkodo,
// nehogy egy szabalytalan keretezes vegtelen ciklusba vigyen.
constexpr uint16_t HTTP_MAX_CHUNKS = HTTP_MAX_PAYLOAD + 2;
// A legmagasabb letezo ciklus index: ot vegpont van, 0..4. A FAILURE_STATE
// mar 4-nel resetel, tehat a plafon a gyakorlatban nem is kot - de ez az a
// szam, ameddig az indexnek egyaltalan ertelme van, es a leptetes ezt mondja ki.
constexpr uint8_t MAX_CYCLE_INDEX = 4;
// A reset kuszobe: az index 3 UTAN, vagyis a 4-es indexu teszt (Google) bukasa
// utan indul a router ujrainditas. Lasd a FAILURE_STATE-et.
constexpr uint8_t RESET_TRIGGER_CYCLE = 3;

struct TestState {
  uint8_t cycleIndex = 0;   // melyik végpont jön; 0..MAX_CYCLE_INDEX
  uint8_t failedCount = 0;  // egymás utáni sikertelen tesztek
  uint8_t resetEvents = 0;  // hány router újraindítás volt ebben a sorozatban
  uint8_t resetStep = 0;    // 0 = tétlen, 1 = fut a reset pulzus
};

struct TimingState {
  uint32_t stateStart = 0;             // állapotgép: aktuális állapot kezdete
  uint32_t resetPulseStart = 0;        // relé kikapcsolás kezdete
  uint32_t startMillis = 0;            // setup() időbélyege
  uint32_t resetBtnDownSince = 0;      // debounce: mióta LOW a reset gomb
  uint32_t wifiResetBtnDownSince = 0;  // debounce: mióta LOW a wifireset gomb
  uint32_t blinkLast = 0;              // hibajelző villogás
  uint32_t fatalStart = 0;             // mióta tart a hibajelzés
};

struct UIFlags {
  // "Printed" jelzők: a loop() másodpercenként sokszor fut, ezek gondoskodnak
  // róla, hogy egy állapotváltás üzenete csak EGYSZER menjen ki a soros portra.
  bool successPrinted = false;
  bool resetPrinted = false;
  bool firstStartPrinted = false;
  bool firstStart = true;          // tart-e még az indulás utáni türelmi idő
  bool blinkOn = false;            // hibajelző LED állapot
};

TestState testState;
TimingState timing;
UIFlags uiFlags;

enum State : uint8_t {
  TESTING_STATE = 0,
  FAILURE_STATE = 1,
  SUCCESS_STATE = 2
};

// Az eszköz vagy a routert figyeli, vagy a Wi-Fi beállító portált szolgálja ki.
enum DeviceMode : uint8_t {
  MODE_MONITOR = 0,
  MODE_CONFIG = 1,
  MODE_FATAL = 2  // a konfiguráció nem tölthető be: a program nem fut tovább
};

// A konfiguráció betöltésének háromféle kimenetele. A hiányzó fájl NEM hiba:
// ez az állapot az első indításnál és a wifireset gomb után is normális.
enum ConfigStatus : uint8_t {
  CONFIG_OK = 0,       // beolvasva (az érték lehet üres is)
  CONFIG_MISSING = 1,  // a fájl nem létezik -> nincs még konfiguráció
  CONFIG_ERROR = 2     // a fájl létezik, de nem olvasható -> végzetes hiba
};

State currentState = TESTING_STATE;
DeviceMode deviceMode = MODE_MONITOR;

// Az aszinkron webszerver callbackjéből nem szabad blokkolni/újraindítani,
// ezért csak jelzünk, az újraindítást a loop() végzi el.
// Sikerült-e a LittleFS csatolása. Ha nem, a beállítások nem menthetők.
bool fsReady = false;

// Statikus IP konfigurációval megyünk-e? Csak akkor igaz, ha a WiFi.config()
// ténylegesen sikerült. DHCP-nél a gateway magától a routertől jött, tehát
// definíció szerint helyes - ott nincs mit ellenőrizni.
bool staticConfigActive = false;

// MEGSZAKITAS-ALAPU GOMBRETESZ
//
// MIERT KELL? A gombokat a sajat varakozo ciklusaink 10 ms-onkent nezik - de
// egy futo HTTP teszt alatt nem tudjak: a http.GET() a core blokkolo hivasa,
// nincs benne visszahivas. Ez az ablak halott DNS mellett 33 masodperc (merve:
// BTN2), es egy rovid koppintas nyom nelkul elveszett benne, mert a lab
// allapotat csak mintavetelkor olvastuk.
//
// A MEGOLDAS NEM az, hogy a megszakitas azonnal cselekszik - az kikerulne a
// debounce-t, es egy zajtuske ujrainditana az eszkozt (lasd B1 teszt). Ehelyett
// a megszakitas MEGMERI a lenyomas hosszat: a lefuto elen jegyzi az idot, a
// felfuton pedig csak akkor reteszel, ha a gomb legalabb BUTTON_DEBOUNCE_MS-ig
// lent volt. Igy a debounce ugyanaz marad, csak hardveresen tortenik - es
// akkor is mukodik, amikor a loop epp nem tud mintavetelezni.
//
// A retesz nem kerul meg semmi mast: a savingConfig kaput es a konfigzarat a
// feldolgozas ugyanugy tiszteletben tartja, tehat egy blokkolo szakasz alatt
// erkezett gombnyomas csak KESIK, de nem vagja felbe a fajlirast.
volatile bool btnResetLatched = false;
volatile bool btnWifiResetLatched = false;
volatile uint32_t btnResetDownAt = 0;
volatile uint32_t btnWifiResetDownAt = 0;

// A korai kilépés próbáinak sebességkorlátozó órái. Nem a TimingState-ben
// vannak, mert az a struct a valódi állapotgép idejeit tartja; ez csak
// "mikor pingettünk utoljára". Kettő kell belőlük: a két várakozás egymás
// után is lefuthat ugyanabban a körben.
uint32_t resetDelayProbeLast = 0;   // FAILURE_STATE, RESET_DELAY
uint32_t firstStartProbeLast = 0;   // handleFirstStart(), firstStartDelay

// A heap felugyelet orai es jelzoi. SZANDEKOSAN fajl-szintu globalisok, nem
// fuggveny-szintu static-ok: a sketch tobbi per-boot allapota (resetDelayProbeLast,
// firstStartProbeLast, testState, timing) is igy all, es a teszt-harness
// hidegindulasa (coldBoot) igy tudja mindet egy helyen visszaallitani. Egy
// rejtett static ugyanezt a szerepet toltene be a valos eszkozon, de a
// forgatokonyvek kozott atszivarogna.
uint32_t heapCheckLast = 0;    // az utolso meres ideje
uint32_t heapLogLast = 0;      // az utolso rendszeres allapotsor ideje
uint8_t  heapCritStreak = 0;   // hany egymas utani kritikus meres volt
bool     heapWarnActive = false;  // szol-e eppen a figyelmeztetes

// Fut-e már a watchdog. Az initWatchdog() a setup() elején fut, de EL IS
// BUKHAT (kikapcsolt TWDT, sikertelen feliratkozás) - és ilyenkor szándékosan
// le is iratkozunk. Feliratkozás nélkül a feedLoopWDT() ESP_ERR_NOT_FOUND-ot
// kapna ("task not found"), amire a core log_e()-t hív: bekapcsolt debug log
// mellett ez 10 ms-onként egy hibasor a soros porton. Ezért etet minden
// feedWatchdog() csak ezen a kapcsolón keresztül.
bool watchdogEnabled = false;

// Watchdog/panic miatti újraindulások számlálója.
// FONTOS: itt RTC_NOINIT_ATTR kell, nem RTC_DATA_ATTR! Az utóbbi csak a deep
// sleepet éli túl, egy watchdog reset ujrainicializalna - épp azt veszítenénk
// el, amit számolni akarunk. A NOINIT viszont bekapcsoláskor határozatlan
// tartalmú, ezért magic értékkel ellenőrizzük az érvényességét.
constexpr uint32_t WDT_COUNTER_MAGIC = 0x42415A53UL;  // "BAZS"
RTC_NOINIT_ATTR uint32_t rtcWdtMagic;
RTC_NOINIT_ATTR uint32_t rtcWdtResets;

// Hány újrapróbálkozási kört tudtunk le eddig. RTC_DATA_ATTR (nem NOINIT):
// a deep sleepet túléli, de bekapcsoláskor és reset gombra nullázódik - a
// felhasználói beavatkozás tiszta 2 napos ablakkal indít.
RTC_DATA_ATTR uint32_t rtcRetryRounds = 0;

// --- Diagnosztikai eseménynapló ---------------------------------------------
// RTC_NOINIT_ATTR: túléli a deep sleepet, a watchdog resetet ÉS a reset gombot
// is - vagyis pont azokat a hibákat, amiket ki akarunk vizsgálni. Csak az
// áramtalanítás törli. Az ESP32-C3-on ~8 KB RTC fast memória van, ez 264 bájt.
enum EventCode : uint8_t {
  EV_BOOT = 1,          // param: reset ok (esp_reset_reason_t)
  EV_WIFI_OK = 2,       // param: kör sorszám, amiben sikerült
  EV_WIFI_LOST = 3,     // param: WiFi.status()
  EV_TEST_FAIL = 4,     // param: teszt ciklus index, 1-alapú (1..5)
  EV_ROUTER_RESET = 5,  // param: hányadik reset esemény
  EV_AP_MODE = 6,       // param: ok (1=nincs SSID 2=auth hiba 3=2 nap letelt
                        //           4=statikus IP rossz: a gateway sem elerheto)
  EV_CONFIG_SAVED = 7,  // param: 0
  EV_SLEEP = 8,         // param: ok (1=retry 2=internet 3=AP timeout 4=fatal)
  EV_FATAL = 9,         // param: ok (1=FS mount 2=konfig olvasás 3=watchdog
                        //           4=wifireset törlés sikertelen
                        //           5=tartósan kevés a szabad heap)
  EV_WDT_RESET = 10,    // param: hányadik watchdog reset
  EV_STUCK_BUTTON = 11, // param: 0 = reset gomb, 1 = wifireset gomb
  EV_GW_UNREACHABLE = 12, // param: 1 = reset elott, 2 = a reset utan is
  EV_LOW_HEAP = 13,     // param: a szabad heap KiB-ban, a kuszob atlepesekor
  EV_HEAP_RESTART = 14  // param: a szabad heap KiB-ban az ujrainditas elott
};

// Pontosan 12 bájt. A kitöltő mező explicit, hogy a RTC memóriában tárolt
// elrendezés akkor se változzon, ha a fordító igazítási szabályai eltérnek -
// és ez most már nem csak az RTC-re igaz: ez a struktúra megy ki bájtról
// bájtra a LittleFS-re mentett naplófájlba is (lásd saveEventLog()), tehát a
// méret és a sorrend a FÁJLFORMÁTUM része. Ha valaha változik, a fájl
// verziószámát (EVFILE_VERSION) is emelni kell.
struct EventEntry {
  uint32_t uptimeSec;
  uint32_t epoch;     // valos ido (unix), 0 ha az NTP meg nem szinkronizalt
  uint16_t param;
  uint8_t code;
  uint8_t reserved;
};

constexpr uint8_t EVLOG_SIZE = 32;
constexpr uint32_t EVLOG_MAGIC = 0x42415A4CUL;  // "BAZL"
RTC_NOINIT_ATTR uint32_t rtcEvMagic;
RTC_NOINIT_ATTR uint32_t rtcEvNext;   // következő írási pozíció (monoton nő)
RTC_NOINIT_ATTR EventEntry rtcEvents[EVLOG_SIZE];

// --- A naplo mentese LittleFS-re -------------------------------------------
//
// MIERT? Az RTC naplo az aramszunetet NEM eli tul - epp azt a hibat nem, ami
// utan a leginkabb tudni akarnank, mi tortent elotte. Ezert a program a
// FONTOS pillanatokban kiirja a naplot a fajlrendszerre is: router reset
// elott, AP modba valtas elott, es az 1 oras alvas elott. Ezek azok a
// pontok, ahol vagy hosszabb ido kovetkezik, vagy az eszkoz beavatkozik -
// mindketto olyan, amit egy kesobbi vizsgalat latni akar.
//
// A fajl a teljes korpuffer pillanatkepe, tehat mindig "az utolso 32 esemeny,
// ahogy a legutobbi fontos pillanatban allt".
constexpr char evLogPath[] = "/evlog.bin";
constexpr uint32_t EVFILE_MAGIC = 0x42415A46UL;   // "BAZF"
constexpr uint16_t EVFILE_VERSION = 1;

// A fajl fejlece. Fix meretu, es a struktura elrendezese a formatum resze.
struct EvFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;        // hany bejegyzes kovetkezik (0..EVLOG_SIZE)
  uint32_t evNextAtSave; // az rtcEvNext erteke a mentes pillanataban
  uint32_t savedEpoch;   // mikor mentettuk (0 ha nem volt NTP)
  uint32_t savedUptime;  // uptime a mentes pillanataban
};

// Az rtcEvNext erteke a legutobbi SIKERES mentesnel. Ket dolgot ad:
//  - nem irunk feleslegesen (ha azota nem tortent esemeny, nincs mit menteni),
//  - es igy a flash kopasa a tenyleges esemenyekhez igazodik.
RTC_NOINIT_ATTR uint32_t rtcSavedEvNext;

// --- A heap miatti ujraindulas atvitele ------------------------------------
//
// MIT KELL ATVINNI? Egy ESP.restart() a RAM-ot torli, tehat minden sima
// globalis elveszik: testState, timing, uiFlags. A tobbsegukert nem kar - a
// timing ujraszamolodik, a firstStart varakozas ujra lefut (es a proba
// perceken belul le is zarja), a cycleIndex/failedCount pedig csak azt
// mondja meg, hol tart az OT teszt kozott.
//
// EGY kivetel van, es annal az elvesztes VISELKEDESI hibat okozna: a
// testState.resetEvents. Ez szamolja, hanyszor indítottuk mar ujra a routert
// ebben a sorozatban, es ez viszi el az eszkozt az otodiknel az 1 oras
// alvasba (internetFailSleep). Ha egy heap-ujraindulas ezt nullazna, a
// szamlalo mindig elolrol kezdene, es az eszkoz VEGTELENUL ujraindithatna a
// routert ahelyett, hogy elalszik. Ezert ezt az egyet atvisszuk.
//
// A 0xFF a "nincs atvitel" jelolese: a setup() egyszer felhasznalja, majd
// vissza is allitja, hogy egy KESOBBI, mas okbol tortent ujraindulas (gomb,
// watchdog) ne allitson vissza elavult erteket.
constexpr uint32_t HEAP_MAGIC = 0x42415A48UL;   // "BAZH"
constexpr uint8_t  CARRY_NONE = 0xFF;
RTC_NOINIT_ATTR uint32_t rtcHeapMagic;
RTC_NOINIT_ATTR uint32_t rtcHeapRestarts;      // egymast koveto heap-ujrainditasok
RTC_NOINIT_ATTR uint8_t  rtcCarryResetEvents;  // atvitt resetEvents (CARRY_NONE = nincs)

volatile bool restartPending = false;
volatile uint32_t restartAt = 0;

// AP beállító mód: mikor aludjon el, ha nem érkezik mentés. Minden HTTP kérés
// kitolja. A savingConfig azt jelzi, hogy épp fájlírás folyik - ilyenkor
// semmiképp nem alszunk el.
volatile uint32_t apDeadline = 0;
volatile bool savingConfig = false;

// Forward declarations (a .ino auto-prototípusok helyett explicit módon)
void printUptime();
void resetbutton();
void wifiresetbutton();
void armButtonLatches();
void restartFromButton(const char* reason);
void doWifiReset();
void blockingDelay(uint32_t duration);
void waitWithButtons(uint32_t duration);
bool waitWithButtonsUntilOnline(uint32_t duration);
bool onlineProbe();
bool onlineProbeDue(uint32_t& lastProbe, uint32_t now);
void lockConfigBeforeShutdown();
void internetFailSleep();
void fatalSleep();
void apSleep();
void touchApDeadline();
void feedWatchdog();
void wifiGiveUp();
bool routerResetAndRetry();
bool wifiAuthFailed();
bool reset_device();
void logEvent(EventCode code, uint16_t param);
bool beginConfigWrite();
void startConfigPortal();
const char* resetReasonName(esp_reset_reason_t r);
void sendConfigForm(AsyncWebServerRequest* request);
void printHtmlEscaped(AsyncResponseStream* r, const char* s);
void enterFatal(const char* reason);
void fatalHalt(const char* reason);
void enterDeepSleep(uint64_t timerUs);
void holdRelayForSleep();
bool stuckCycleAlreadyLogged(uint16_t which);
bool lastEventWas(uint8_t code);
int pressedButtonNow();
void checkHeap(uint32_t now);
bool saveEventLog(const char* reason);
bool loadEventLogHeader(EvFileHeader& fej);
bool loadEventLogEntries(const EvFileHeader& fej, EventEntry* ki);
uint32_t nowEpoch();
void startNtp();
void initHeapState();
void applyHeapCarry();
bool initWiFi();
bool reconnectWifi();
bool writeConfigValue(fs::FS& fs, const char* path, const char* message);
bool isUsableIPv4(const IPAddress& addr);
bool gatewayUnreachable();
bool testInternetPing(const IPAddress& target, const char* targetName);
bool encodeSecret(const char* plain, char* out, size_t outSize);
void decodeSecretInPlace(char* buf);

// Esemény rögzítése a körpufferbe. Nem allokál, nem blokkol.
//
// Két task is hív: a loop task mellett az async_tcp task is (a POST kezelő
// CONFIG_SAVED bejegyzése). A pozíció léptetése és a slot írása együtt nem
// atomi, ezért kritikus szakasz védi - enélkül két egyidejű hívás ugyanabba
// a slotba írhatna, vagy egy bejegyzés elveszne. A szakasz rövid (a memset
// csak a legelső híváskor fut), a megszakítás-tiltás belefér.
portMUX_TYPE evLogMux = portMUX_INITIALIZER_UNLOCKED;
void logEvent(EventCode code, uint16_t param) {
  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic != EVLOG_MAGIC) {
    rtcEvMagic = EVLOG_MAGIC;
    rtcEvNext = 0;
    rtcSavedEvNext = 0;
    memset(rtcEvents, 0, sizeof(rtcEvents));
  }
  EventEntry& e = rtcEvents[rtcEvNext % EVLOG_SIZE];
  e.uptimeSec = (uint32_t)(esp_timer_get_time() / 1000000);
  // Ha az NTP mar szinkronizalt, a valos idot is eltesszuk. Enelkul csak
  // uptime van, ami minden indulaskor nullarol kezd - ket bootolas esemenyei
  // igy nem rendezhetok egymashoz. A time() nem blokkol es nem allokal.
  e.epoch = nowEpoch();
  e.code = (uint8_t)code;
  e.param = param;
  e.reserved = 0;
  rtcEvNext++;
  portEXIT_CRITICAL(&evLogMux);
}

// Az indulás okának EMBERI neve. A /log oldalon eddig a nyers enum-szám
// szerepelt ("Utolso indulas oka: 8"), amihez a felhasználónak az ESP-IDF
// fejlécét kellett volna kikeresnie - épp azt a diagnózist nehezítve, amiért
// az oldal egyáltalán van. A számot zárójelben megtartjuk, hogy egy
// hibajelentés továbbra is egyértelmű legyen.
const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:    return "bekapcsolas / aramtalanitas";
    case ESP_RST_EXT:        return "kulso reset lab";
    case ESP_RST_SW:         return "szoftveres ujrainditas (ESP.restart)";
    case ESP_RST_PANIC:      return "PANIC - a program osszeomlott";
    case ESP_RST_INT_WDT:    return "megszakitas-watchdog";
    case ESP_RST_TASK_WDT:   return "TASK WATCHDOG - a loop megallt";
    case ESP_RST_WDT:        return "egyeb watchdog";
    case ESP_RST_DEEPSLEEP:  return "ebredes deep sleepbol";
    case ESP_RST_BROWNOUT:   return "BROWNOUT - leesett a tapfeszultseg";
    case ESP_RST_CPU_LOCKUP: return "CPU lefagyas";
    default:                 return "ismeretlen";
  }
}

const char* eventName(uint8_t code) {
  switch (code) {
    case EV_BOOT: return "BOOT";
    case EV_WIFI_OK: return "WIFI OK";
    case EV_WIFI_LOST: return "WIFI LOST";
    case EV_TEST_FAIL: return "TEST FAIL";
    case EV_ROUTER_RESET: return "ROUTER RESET";
    case EV_AP_MODE: return "AP MODE";
    case EV_CONFIG_SAVED: return "CONFIG SAVED";
    case EV_SLEEP: return "SLEEP";
    case EV_FATAL: return "FATAL";
    case EV_WDT_RESET: return "WDT RESET";
    case EV_STUCK_BUTTON: return "STUCK BUTTON";
    case EV_GW_UNREACHABLE: return "GW UNREACH";
    case EV_LOW_HEAP: return "LOW HEAP";
    case EV_HEAP_RESTART: return "HEAP RESTART";
    default: return "?";
  }
}

// --- A mentett jelszó összekeverése ----------------------------------------
//
// Cél, pontosan körülhatárolva: egy flash dumpon futtatott `strings` NE adjon
// használható jelszót, és egy kimásolt /pass.txt más lapkán se működjön.
//
// Amit NEM ad: ez nem titkosítás. Aki kódot tud futtatni az eszközön (a C3-ban
// beépített USB Serial/JTAG-gel vagy saját sketch-csel), az a visszafejtett
// jelszót kiolvassa a RAM-ból - a művelet ugyanis magán az eszközön történik.
// Az egyetlen valódi védelem a flash titkosítás (eFuse-ban tárolt kulccsal).
//
// Formátum: "v1:" + kisbetűs hexa. Az előtag nélküli fájl régi, sima szöveges
// mentés; azt továbbra is elfogadjuk, különben egy frissítés használhatatlanná
// tenné a már beállított eszközöket.
//
// A kulcsfolyam magjában ott van az eFuse MAC is (esp_efuse_mac_get_default(),
// Esp.cpp). Az eFuse NEM a flashben van, tehát egy önmagában kimásolt
// flash-tartalom kevés hozzá, és a nyilvános forráskódból írt általános
// dekóder sem elég: az adott chip is kell.
constexpr uint32_t SECRET_SALT = 0x42415A53UL;  // "BAZS"
constexpr char SECRET_PREFIX[] = "v1:";
constexpr size_t SECRET_PREFIX_LEN = sizeof(SECRET_PREFIX) - 1;
// "v1:" + 2 hexa jegy jelszó-bájtonként
constexpr size_t SECRET_ENC_MAX = SECRET_PREFIX_LEN + 2 * PASS_MAX_LEN;

uint32_t secretSeed() {
  const uint64_t mac = ESP.getEfuseMac();  // 6 bájt, a felső 2 nulla
  const uint32_t seed = SECRET_SALT ^ (uint32_t)mac ^ (uint32_t)(mac >> 32);
  // A xorshift a 0 állapotból soha nem lép ki - ezt ki kell zárni.
  return seed != 0 ? seed : SECRET_SALT;
}

// Determinisztikus kulcsfolyam. Pozíciófüggő, tehát az ismétlődő karakterek
// sem adnak ismétlődő bájtokat a fájlban.
uint32_t xorshift32(uint32_t& x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

bool encodeSecret(const char* plain, char* out, size_t outSize) {
  const size_t len = strlen(plain);
  if (outSize < SECRET_PREFIX_LEN + 2 * len + 1) {
    return false;
  }
  memcpy(out, SECRET_PREFIX, SECRET_PREFIX_LEN);
  uint32_t state = secretSeed();
  // NEM "HEX": a Print.h-ban az egy makro (#define HEX 16), tehat abbol
  // "static const char 16[]" lenne. A core makrói (HEX, DEC, OCT, BIN) minden
  // sketchre ravonatkoznak - a lokalis nevek nem utkozhetnek veluk.
  static const char kHexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    const uint8_t b = (uint8_t)plain[i] ^ (uint8_t)(xorshift32(state) & 0xFF);
    out[SECRET_PREFIX_LEN + 2 * i]     = kHexDigits[b >> 4];
    out[SECRET_PREFIX_LEN + 2 * i + 1] = kHexDigits[b & 0x0F];
  }
  out[SECRET_PREFIX_LEN + 2 * len] = '\0';
  return true;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // szándékosan csak kisbetűs: ezt írjuk ki
}

// Helyben dekódol. Ha a tartalom nem a mi formátumunk, VÁLTOZATLANUL hagyja -
// így a régi, sima szöveges mentések is működnek, és az sem baj, ha valakinek
// történetesen "v1:" a jelszava.
//
// Szándékosan NEM végzetes hiba a hibás tartalom: rossz jelszóval a Wi-Fi
// egyszerűen nem jön össze, és az eszköz a szokásos úton AP módba kerül, ahol
// újra beállítható. Ez öngyógyul, a villogó LED nem.
void decodeSecretInPlace(char* buf) {
  if (strncmp(buf, SECRET_PREFIX, SECRET_PREFIX_LEN) != 0) {
    return;  // régi, sima szöveges mentés
  }
  const char* hex = buf + SECRET_PREFIX_LEN;
  const size_t hexLen = strlen(hex);
  if (hexLen % 2 != 0) {
    return;
  }
  for (size_t i = 0; i < hexLen; i++) {
    if (hexVal(hex[i]) < 0) {
      return;
    }
  }
  // A kiírási index (i) mindig kisebb az olvasásinál (3 + 2i), ezért a helyben
  // dekódolás előrefelé haladva biztonságos.
  uint32_t state = secretSeed();
  const size_t n = hexLen / 2;
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = (uint8_t)((hexVal(hex[2 * i]) << 4) | hexVal(hex[2 * i + 1]));
    buf[i] = (char)(b ^ (uint8_t)(xorshift32(state) & 0xFF));
  }
  buf[n] = '\0';
}

// Használható-e ez a cím statikus IPv4 konfigurációnak?
//
// Az IPAddress::fromString() az IPv4 után IPv6-ot is megpróbál (IPAddress.cpp),
// ezért a "::1" vagy a "fe80::1" is érvényesnek látszik - és mindkettő befér a
// 15 karakteres mezőbe. Az eszköz viszont végig IPv4-en dolgozik (a gateway
// pingje, a HTTP tesztek, az 1.1.1.1-es tartalék DNS, a /24-es maszk), a
// WiFi.config() pedig az IPAddress uint32_t konverzióját használja
// (NetworkInterface.cpp:390), ami IPv6-ra 0-t ad
// (IPAddress.h:83). Vagyis egy IPv6 cím csendben DHCP-t vagy - ami rosszabb -
// egy 0.0.0.0-s gateway-t és DNS-t eredményezne. A 0.0.0.0 ugyanezt jelenti,
// ezért az sem fogadható el.
bool isUsableIPv4(const IPAddress& addr) {
  return (uint32_t)addr != 0;
}

// Whitespace levágása helyben, allokáció nélkül
void trimInPlace(char* s) {
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[--len] = '\0';
  }
  size_t start = 0;
  while (s[start] != '\0' && isspace((unsigned char)s[start])) {
    start++;
  }
  if (start > 0) {
    memmove(s, s + start, len - start + 1);
  }
}

// Initialize LittleFS
bool initLittleFS() {
  if (!LittleFS.begin(true)) {
    // A begin() csak ESP_FAIL esetén próbál formázni. Ha a partíció egyáltalán
    // nincs meg, ESP_ERR_NOT_FOUND jön, és formázás nélkül elbukik.
    Serial.println("LittleFS mount FAILED (a formázási kísérlet után is).");
    Serial.println("Valószínű ok: a használt partíciós séma nem tartalmaz");
    Serial.println("'spiffs' cimkéju partíciót. Használd a partitions_custom.csv-t,");
    Serial.println("vagy az Arduino IDE-ben egy SPIFFS-et tartalmazó sémát");
    Serial.println("(Tools > Partition Scheme).");
    return false;
  }
  Serial.print("LittleFS mounted, used ");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" / ");
  Serial.print(LittleFS.totalBytes());
  Serial.println(" bytes");
  return true;
}

// A kiírt tartalom ellenőrzése visszaolvasással. Erre azért van szükség, mert
// a File::close() és a File::flush() is void: a lezáráskor jelentkező hibát
// (pl. megtelt fájlrendszer) másképp nem lehetne észrevenni.
bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len) {
  File file = fs.open(path);
  if (!file) {
    return false;
  }
  if (file.size() != len) {
    file.close();
    return false;
  }
  char chunk[32];
  size_t off = 0;
  while (off < len) {
    const size_t want = (len - off > sizeof(chunk)) ? sizeof(chunk) : (len - off);
    const size_t got = file.read((uint8_t*)chunk, want);
    if (got != want || memcmp(chunk, value + off, got) != 0) {
      file.close();
      return false;
    }
    off += got;
  }
  file.close();
  return true;
}

// Egy konfigurációs érték beolvasása fix méretű bufferbe (String allokáció nélkül)
ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize) {
  out[0] = '\0';

  // A hiányzó fájl nem hiba: első indításkor és wifireset után is ez a helyzet.
  if (!fs.exists(path)) {
    Serial.printf("- %s missing (no config yet)\r\n", path);
    return CONFIG_MISSING;
  }

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.printf("- failed to open %s for reading\r\n", path);
    if (file) {
      file.close();
    }
    return CONFIG_ERROR;
  }
  // Méret szerint olvasunk: a Stream::readBytesUntil() EOF-nál kivárná a teljes
  // 1 másodperces stream-timeoutot, fájlonként (indulásnál ez 4 mp veszteség).
  const size_t fileSize = file.size();
  const size_t toRead = (fileSize < outSize - 1) ? fileSize : outSize - 1;
  const size_t n = file.read((uint8_t*)out, toRead);
  file.close();
  if (n != toRead) {
    Serial.printf("- short read on %s (%u / %u)\r\n", path, (unsigned)n, (unsigned)toRead);
    out[0] = '\0';
    return CONFIG_ERROR;
  }
  out[n] = '\0';

  char* nl = strchr(out, '\n');  // csak az első sor érdekel
  if (nl != nullptr) {
    *nl = '\0';
  }
  trimInPlace(out);
  // Az üres tartalom is érvényes eredmény: a wifireset csonkolt fájlt hagy hátra.
  return CONFIG_OK;
}

// Írás ellenőrzéssel. true csak akkor, ha a tartalom vissza is olvasható.
bool writeConfigValue(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);
  const size_t len = strlen(message);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return false;
  }
  // Üres értéknél a print() jogosan ad 0-t; ez nem hiba, csak csonkolás.
  const size_t written = (len > 0) ? file.print(message) : 0;
  file.flush();
  file.close();

  if (written != len) {
    Serial.print("- short write: ");
    Serial.print((unsigned)written);
    Serial.print(" / ");
    Serial.print((unsigned)len);
    Serial.println(" bájt (megtelt a fájlrendszer?)");
    return false;
  }
  if (!fileMatches(fs, path, message, len)) {
    Serial.println("- verify FAILED: a visszaolvasott tartalom nem egyezik");
    return false;
  }
  Serial.println("- file written");
  return true;
}

// Érték törlése. Először csonkolással próbáljuk; ha az nem megy, a fájlt
// magát töröljük - a readConfigValue() a hiányzó fájlt is "nincs érték"-ként
// kezeli, tehát a végeredmény ugyanaz.
bool clearConfigValue(fs::FS& fs, const char* path) {
  Serial.printf("Clearing file: %s\r\n", path);
  if (writeConfigValue(fs, path, "")) {
    Serial.println("- file cleared");
    return true;
  }
  if (fs.remove(path)) {
    Serial.println("- file removed instead");
    return true;
  }
  Serial.println("- FAILED to clear file!");
  return false;
}

// Végzetes hiba: a konfiguráció nem tölthető be. Ilyenkor a program nem fut
// tovább (nincs teszt, nincs relé kapcsolás, nincs elalvás), csak jelez.
void enterFatal(const char* reason) {
  deviceMode = MODE_FATAL;
  timing.fatalStart = millis();
  timing.blinkLast = timing.fatalStart;
  digitalWrite(relayPin, LOW);  // a router mindenképp kapjon áramot
  Serial.println();
  Serial.println("!!! VEGZETES HIBA !!!");
  Serial.println(reason);
  Serial.println("A program leallt, mindket LED gyorsan villog.");
  Serial.println("Reset gomb: ujraindítas. Wifireset gomb: mentett adatok torlese.");
  Serial.print(FATAL_SLEEP_AFTER_MS / 60000);
  Serial.println(" perc mulva az eszkoz elalszik (magatol nem ebred fel).");
}

// Watchdog/panic miatti újraindulások figyelése. Ha a program ismételten
// megakad, az újraindítgatás önmagában nem megoldás - inkább jelezzünk.
// A heap allapot RTC blokkjanak felelesztese. Ugyanaz a minta, mint a
// watchdog szamlalonal: bekapcsolas utan a NOINIT terulet tartalma szemet.
// A rendszerora aktualis erteke, ha az NTP mar szinkronizalt - kulonben 0.
// Nem blokkol, nem allokal, es szinkron nelkul is biztonsagos.
uint32_t nowEpoch() {
  const time_t t = time(nullptr);
  if (t < (time_t)NTP_MIN_VALID_EPOCH) {
    return 0;
  }
  return (uint32_t)t;
}

// Az SNTP kliens elinditasa. CSAK elinditja: a valasz a hatterben erkezik, a
// hivas nem var ra. Tobbszor is hivhato (ujracsatlakozasnal), a kliens
// ujraindul. A rendszeroraval egyutt a nyari idoszamitas kezelese is beall.
//
// FONTOS: a valos ido a DEEP SLEEPET TULELI. Az esp_timer (es igy a millis())
// ebredeskor nullarol indul, a gettimeofday() alapu rendszerora viszont az RTC
// orabol jon, tehat egy 1 oras alvas utan is jo idot mutat - epp ezert
// hasznalhato a naplo bejegyzesek rendezesere bootolasokon at.
void startNtp() {
  configTzTime(NTP_TZ, NTP_SERVER);
  printUptime();
  Serial.print("NTP inditva: ");
  Serial.println(NTP_SERVER);
}

// Egy idopont ember altal olvashato alakja. Ha nincs valos ido, a hivo
// dontse el, mit ir helyette - ez a fuggveny csak akkor ad true-t, ha tenyleg
// van mit formazni.
bool formatEpoch(uint32_t epoch, char* out, size_t outSize) {
  if (epoch < NTP_MIN_VALID_EPOCH || outSize < 20) {
    return false;
  }
  const time_t t = (time_t)epoch;
  struct tm tmv;
  if (localtime_r(&t, &tmv) == nullptr) {
    return false;
  }
  strftime(out, outSize, "%Y-%m-%d %H:%M:%S", &tmv);
  return true;
}

// A napló kiírása a fájlrendszerre.
//
// A ZAR. A muvelet ATOMIKUSAN szerzi meg a konfiguraciós zárat, ugyanazt,
// amit a webes mentés és a wifireset gomb használ. Amíg a zár a miénk:
//   - nem alszik el az eszköz és nem indul újra (a leállási út megvárja),
//   - a gombok nem szólnak közbe,
//   - és másik fájlírás sem indulhat.
// Ha a zár épp foglalt, NEM várunk rá: a mentés kimarad. Ez helyes döntés -
// a napló diagnosztika, nem szabad miatta blokkolni egy fontos műveletet
// (router reset, alvás), és a következő fontos pillanatban úgyis próbáljuk.
//
// A zárat a végén FELOLDJUK - eltérően a leállási úttól, ami már nem tér vissza.
bool saveEventLog(const char* reason) {
  if (!fsReady) {
    return false;   // nincs hova irni; ez nem hiba, csak nincs mentes
  }

  // Nincs mit menteni? Akkor a flasht sem koptatjuk. Ez nem optimalizacio,
  // hanem a kopas es a tenyleges esemenyek osszehangolasa.
  uint32_t evTotal;
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  portEXIT_CRITICAL(&evLogMux);
  if (evTotal == 0 || evTotal == rtcSavedEvNext) {
    return false;
  }

  if (!beginConfigWrite()) {
    printUptime();
    Serial.println("A naplo mentese kimarad: eppen mas fajliras folyik.");
    return false;
  }

  // Pillanatkep a mux alatt, ugyanugy, mint a /log oldalon: a naploba az
  // async_tcp task is ir, tehat a pozicio es a tartalom egyutt nem olvashato
  // atomian.
  EvFileHeader fej;
  EventEntry masolat[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  fej.evNextAtSave = rtcEvNext;
  memcpy(masolat, rtcEvents, sizeof(masolat));
  portEXIT_CRITICAL(&evLogMux);

  fej.magic = EVFILE_MAGIC;
  fej.version = EVFILE_VERSION;
  fej.count = (uint16_t)(fej.evNextAtSave < EVLOG_SIZE ? fej.evNextAtSave : EVLOG_SIZE);
  fej.savedEpoch = nowEpoch();
  fej.savedUptime = (uint32_t)(esp_timer_get_time() / 1000000);

  // A bejegyzeseket a LEGREGEBBITOL a legujabbig irjuk ki, kiegyenesitve -
  // igy az olvasonak nem kell tudnia, hol tartott a korpuffer. Ehhez NEM
  // masolunk egy ujabb 384 bajtos verempuffert: a korpuffer legfeljebb ket
  // OSSZEFUGGO szakaszra bomlik (a kezdoponttol a tomb vegeig, majd a tomb
  // elejetol), es ezt a kettot irjuk ki egymas utan. A loop task verme igy
  // 384 bajttal kevesebbet visz.
  const uint32_t elso = fej.evNextAtSave - fej.count;
  const uint16_t kezdet = (uint16_t)(elso % EVLOG_SIZE);
  const uint16_t elsoDb = (uint16_t)((kezdet + fej.count <= EVLOG_SIZE)
                                     ? fej.count : (EVLOG_SIZE - kezdet));
  const uint16_t masodikDb = (uint16_t)(fej.count - elsoDb);

  printUptime();
  Serial.print("Naplo mentese a fajlrendszerre (");
  Serial.print(reason);
  Serial.println(")...");

  bool ok = false;
  File f = LittleFS.open(evLogPath, FILE_WRITE);
  if (!f) {
    Serial.println("- a naplofajl nem nyithato irasra");
  } else {
    const size_t fejBytes = f.write((const uint8_t*)&fej, sizeof(fej));
    size_t adatBytes = 0;
    if (elsoDb) {
      adatBytes += f.write((const uint8_t*)&masolat[kezdet],
                           sizeof(EventEntry) * elsoDb);
    }
    if (masodikDb) {
      adatBytes += f.write((const uint8_t*)&masolat[0],
                           sizeof(EventEntry) * masodikDb);
    }
    f.flush();
    f.close();
    const size_t vart = sizeof(fej) + sizeof(EventEntry) * fej.count;
    if (fejBytes + adatBytes != vart) {
      Serial.print("- rovid iras: ");
      Serial.print((unsigned)(fejBytes + adatBytes));
      Serial.print(" / ");
      Serial.print((unsigned)vart);
      Serial.println(" bajt (megtelt a fajlrendszer?)");
    } else {
      // VISSZAOLVASAS. Ugyanaz az elv, mint a writeConfigValue()-nal: a siker
      // nem az, hogy az iras nem panaszkodott, hanem hogy a tartalom tenyleg
      // ott van. Csak a fejlecet olvassuk vissza - ha az ep, a fajlmeret
      // pedig egyezik, a tartalom is kiirodott.
      File v = LittleFS.open(evLogPath, "r");
      if (!v) {
        Serial.println("- verify: a fajl nem nyithato olvasasra");
      } else {
        EvFileHeader vissza;
        const size_t olvasott = v.read((uint8_t*)&vissza, sizeof(vissza));
        const size_t meret = v.size();
        v.close();
        if (olvasott != sizeof(vissza) || meret != vart
            || vissza.magic != EVFILE_MAGIC || vissza.count != fej.count
            || vissza.evNextAtSave != fej.evNextAtSave) {
          Serial.println("- verify FAILED: a visszaolvasott fejlec nem egyezik");
        } else {
          ok = true;
        }
      }
    }
  }

  if (ok) {
    rtcSavedEvNext = fej.evNextAtSave;
    Serial.print("- naplo mentve, ");
    Serial.print((unsigned)fej.count);
    Serial.println(" bejegyzes");
  } else {
    // A SIKERTELENSEG NEM VEGZETES. A naplo diagnosztika: ha nem sikerul
    // kiirni, az RTC-ben tovabbra is ott van, es a program dolga (router
    // reset, alvas) fontosabb. Csak szolunk rola.
    Serial.println("- a naplo mentese NEM sikerult, az RTC naplo ettol meg el");
  }
  savingConfig = false;   // a zar feloldasa - itt meg VISSZATERUNK
  return ok;
}

// A mentett naplo FEJLECENEK beolvasasa es ellenorzese. Igaz, ha a fajl
// letezik, ep, es van benne legalabb egy bejegyzes. Minden mas esetben (nincs
// fajl, ures fajl, rossz magic, csonka tartalom, ismeretlen verzio, a
// fejlecben igert darabszamnal rovidebb fajl) hamis - a hivo ilyenkor az RTC
// naplot hasznalja, kulon hibauzenet nelkul.
//
// MIERT KULON A FEJLEC ES A TARTALOM? Az async_tcp task verme veges, es 32
// bejegyzes mar 384 bajt. Ha itt is puffert kernenk, a /log kezelo EGYSZERRE
// ket ilyet tartana (az RTC pillanatkepet es a fajlet). Igy viszont eloszb
// eldontjuk a fejlecbol, melyik forras kell - es csak azt toltjuk be, EGY
// pufferbe.
bool loadEventLogHeader(EvFileHeader& fej) {
  if (!fsReady || !LittleFS.exists(evLogPath)) {
    return false;
  }
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }
  const size_t meret = f.size();
  if (meret < sizeof(EvFileHeader)
      || f.read((uint8_t*)&fej, sizeof(fej)) != sizeof(fej)) {
    f.close();   // ures vagy csonka fajl
    return false;
  }
  f.close();
  if (fej.magic != EVFILE_MAGIC || fej.version != EVFILE_VERSION
      || fej.count == 0 || fej.count > EVLOG_SIZE) {
    return false;
  }
  // A fejlec tobbet igerhet, mint amennyi tenyleg ott van (megtelt fajlrendszer,
  // felbeszakadt iras). Ezt MOST ellenorizzuk, hogy a betoltes mar biztos legyen.
  return meret >= sizeof(EvFileHeader) + sizeof(EventEntry) * fej.count;
}

// A bejegyzesek betoltese - csak akkor, ha a fejlec mar rendben volt.
bool loadEventLogEntries(const EvFileHeader& fej, EventEntry* ki) {
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }
  EvFileHeader eldobando;    // a fejlecet atugorjuk (a stream nem kereshet)
  if (f.read((uint8_t*)&eldobando, sizeof(eldobando)) != sizeof(eldobando)) {
    f.close();
    return false;
  }
  const size_t kell = sizeof(EventEntry) * fej.count;
  const size_t olvasott = f.read((uint8_t*)ki, kell);
  f.close();
  return olvasott == kell;
}

void initHeapState() {
  if (rtcHeapMagic != HEAP_MAGIC) {
    rtcHeapMagic = HEAP_MAGIC;
    rtcHeapRestarts = 0;
    rtcCarryResetEvents = CARRY_NONE;
  }
}

// Az atvitt allapot FELHASZNALASA - pontosan egyszer. A visszaallitas utan a
// jelolot toroljuk, kulonben egy kesobbi, mas okbol tortent ujraindulas
// (gombnyomas, watchdog) egy regi resetEvents erteket tamasztana fel.
void applyHeapCarry() {
  if (rtcCarryResetEvents == CARRY_NONE) {
    return;
  }
  testState.resetEvents = rtcCarryResetEvents;
  rtcCarryResetEvents = CARRY_NONE;
  printUptime();
  Serial.print("Heap miatti ujraindulas utan folytatjuk: router reset szamlalo = ");
  Serial.print(testState.resetEvents);
  Serial.print(" / ");
  Serial.println(maxfailureEvents);
}

// A heap allapotanak nyomon kovetese: mereskovetes, ritkitott soros kiiras, es
// vegso esetben onkentes ujraindulas.
//
// A KIIRAS RITKITASA. A meres 10 mp-enkent fut, a rendszeres allapotsor
// viszont csak felorankent - ez 0,03 sor/perc, vagyis a 30 sor/perces
// koltsegvetesbol semmit nem visz el. A figyelmeztetes ezen felul jon, de
// csak a kuszob ATLEPESEKOR (nem minden korben) - ugyanaz a "csak a sorozat
// elso tagja" szabaly, mint a TEST FAIL / WIFI LOST eseteben.
void checkHeap(uint32_t now) {
  if (now - heapCheckLast < HEAP_CHECK_INTERVAL_MS) {
    return;
  }
  heapCheckLast = now;

  const uint32_t szabad = ESP.getFreeHeap();
  const uint32_t tomb   = ESP.getMaxAllocHeap();

  // Rendszeres allapotsor. A HARMADIK ertek (a valaha volt legkisebb) a
  // legfontosabb: egy lassu szivargas ebbol latszik meg akkor is, ha a
  // pillanatnyi ertek epp rendben van.
  if (now - heapLogLast >= HEAP_LOG_INTERVAL_MS) {
    heapLogLast = now;
    printUptime();
    Serial.printf("Heap: szabad %u B, legnagyobb tomb %u B, valaha volt legkisebb %u B\n",
                  (unsigned)szabad, (unsigned)tomb, (unsigned)ESP.getMinFreeHeap());
  }

  // Figyelmeztetes - csak az ATLEPESKOR. A heapWarnActive jelzo tartja szamon,
  // hogy a figyelmeztetes mar szol-e; visszaallni is csak akkor lehet, ha a
  // heap ERDEMBEN (10%-kal) a kuszob fole ment, hogy a kuszob korul
  // ingadozva ne kapcsolgasson oda-vissza.
  if (!heapWarnActive && szabad < HEAP_WARN_BYTES) {
    heapWarnActive = true;
    logEvent(EV_LOW_HEAP, (uint16_t)(szabad / 1024));
    printUptime();
    Serial.printf("FIGYELEM: alacsony a szabad heap (%u B), a kuszob %u B.\n",
                  (unsigned)szabad, (unsigned)HEAP_WARN_BYTES);
  } else if (heapWarnActive && szabad > HEAP_WARN_BYTES + HEAP_WARN_BYTES / 10) {
    heapWarnActive = false;
    printUptime();
    Serial.printf("A szabad heap visszaallt (%u B).\n", (unsigned)szabad);
  }

  // Kritikus szint. KI KELL TARTANIA: egy pillanatnyi melypont (epp futo HTTP
  // keres, osszeallitas alatt levo valasz) nem szivargas.
  const bool kritikus = (szabad < HEAP_CRIT_BYTES) || (tomb < HEAP_CRIT_BLOCK_BYTES);
  if (!kritikus) {
    heapCritStreak = 0;
    return;
  }
  // A szamlalo TELITODIK, nem no tovabb. Ez akkor szamit, ha az ujraindulas
  // epp tiltott (AP mod, fajliras, rele impulzus): ilyenkor a kritikus
  // allapot sokaig allhat, es egy korlatlanul novo uint8_t 255 utan
  // korbefordulna nullara - vagyis epp a legrosszabb pillanatban ejtene el a
  // mar kiszolgalt varakozast.
  if (heapCritStreak < HEAP_CRIT_SAMPLES) {
    heapCritStreak++;
    return;
  }

  // MIKOR NEM SZABAD UJRAINDULNI. Mind a negy kizaras arrol szol, hogy az
  // ujraindulas ne rontson tobbet, mint amennyit javit:
  //
  //  - AP beallito modban a felhasznalo epp a portalon dolgozik; az
  //    ujraindulas eldobna a beirt adatokat. Nem is maradunk igy orokre: a
  //    portal 5 perc tetlenseg utan ugyis elalszik, abbol pedig friss
  //    bootolas jon.
  //  - MODE_FATAL-ban a program mar nem fut allapotgepet, es 5 perc mulva
  //    elalszik - ott is jon a friss bootolas.
  //  - Fajliras kozben soha (ezt a zar biztositja lejjebb is).
  //  - A rele impulzusa alatt sem: a router epp aram nelkul van, es az
  //    ujraindulas felbevagna a 90 mp-es pulzust.
  if (deviceMode != MODE_MONITOR || savingConfig || restartPending
      || testState.resetStep != 0) {
    return;
  }

  // Ha az ujraindulas sem segit, a boot loop rosszabb, mint a megallas.
  if (rtcHeapRestarts >= MAX_HEAP_RESTARTS) {
    logEvent(EV_FATAL, 5);
    enterFatal("Tartosan keves a szabad heap - az ujrainditas sem segitett.");
    return;
  }

  // Az ATVITEL beallitasa, MIELOTT ujraindulnank. Lasd a rtcCarryResetEvents
  // leirasat: ez az egyetlen sima globalis, aminek az elvesztese viselkedesi
  // hibat okozna.
  rtcCarryResetEvents = testState.resetEvents;
  rtcHeapRestarts++;
  logEvent(EV_HEAP_RESTART, (uint16_t)(szabad / 1024));
  printUptime();
  Serial.printf("KRITIKUS: a szabad heap %u B (legnagyobb tomb %u B) %u meresen at.\n",
                (unsigned)szabad, (unsigned)tomb, (unsigned)HEAP_CRIT_SAMPLES);
  Serial.print("Onkentes ujraindulas, sorszam: ");
  Serial.print(rtcHeapRestarts);
  Serial.print(" / ");
  Serial.println(MAX_HEAP_RESTARTS);

  // A zarat itt is ATOMIKUSAN szerezzuk meg, ugyanazert, mint minden mas
  // leallasnal: a kiirasok es az ESP.restart() kozott meg elindulhatna egy
  // mentes, amit az ujraindulas felbevagna. (Lasd lockConfigBeforeShutdown().)
  lockConfigBeforeShutdown();
  Serial.flush();
  ESP.restart();
}

void checkWatchdogResets() {
  const esp_reset_reason_t reason = esp_reset_reason();

  if (rtcWdtMagic != WDT_COUNTER_MAGIC) {
    // Első indulás bekapcsolás után: a NOINIT terület tartalma szemét.
    rtcWdtMagic = WDT_COUNTER_MAGIC;
    rtcWdtResets = 0;
  }

  // Minden olyan ok, ami azt jelenti: a program hibásan viselkedett.
  const bool abnormal = (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT
                         || reason == ESP_RST_WDT || reason == ESP_RST_PANIC
                         || reason == ESP_RST_CPU_LOCKUP);

  if (abnormal) {
    rtcWdtResets++;
    logEvent(EV_WDT_RESET, (uint16_t)rtcWdtResets);
    Serial.print("Watchdog/panic miatti ujrainditas, sorszam: ");
    Serial.print(rtcWdtResets);
    Serial.print(" / ");
    Serial.println(MAX_WDT_RESETS);
    if (rtcWdtResets >= MAX_WDT_RESETS) {
      rtcWdtResets = 0;  // az alvás után tiszta lappal induljon
      logEvent(EV_FATAL, 3);
      enterFatal("Tul sok watchdog miatti ujrainditas - a program instabil.");
    }
  } else if (reason == ESP_RST_POWERON || reason == ESP_RST_EXT
             || reason == ESP_RST_BROWNOUT) {
    // Áramtalanítás vagy külső reset: emberi beavatkozás, tiszta lap.
    rtcWdtResets = 0;
  }
}

// Lefagyás elleni védelem.
//
// Az ESP-IDF task watchdogja alapból FUT (ESP_TASK_WDT_INIT=y, 5 mp), de két
// okból nem véd meg minket:
//   1. az Arduino loop taskja nincs ráiratkozva (main.cpp: loopTaskWDTEnabled
//      = false), tehát a loop() megakadását észre sem veszi;
//   2. a panic beállítás forrásfüggő: az IDF Kconfig alapértéke 'n' (timeoutkor
//      csak kiír egy figyelmeztetést, nem indít újra), a kész Arduino libek
//      viszont bekapcsolják (CONFIG_ESP_TASK_WDT_PANIC=y, a lib-builder
//      defconfig.common:21 sora). Nem hagyatkozunk egyikre sem.
// Ezért kifejezetten beállítjuk mindkettőt.
//
// idle_core_mask = 0: csak a saját loop taskunkat figyeltetjük. Az idle task
// figyelése itt kifejezetten káros lenne, mert a firmware szándékosan blokkol
// percekig (90 mp-es relé pulzus), és egy hosszú timeout mellett is kockázatos
// újraindítási hurkot okozna.
void initWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WDT_TIMEOUT_MS;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;

  // 1. A TWDT-nek LÉTEZNIE kell, mielőtt feliratkozunk rá. Az Arduino alapból
  //    inicializálja (ESP_TASK_WDT_INIT=y), de ha valaki kikapcsolja, az
  //    esp_task_wdt_add() ESP_ERR_INVALID_STATE-et adna - az enableLoopWDT()
  //    viszont void, tehát ezt a hibát csendben elnyelné (esp32-hal-misc.c).
  esp_err_t initErr = ESP_OK;
  if (esp_task_wdt_status(NULL) == ESP_ERR_INVALID_STATE) {
    initErr = esp_task_wdt_init(&cfg);
    // Az init figyelt task nélkül NEM indítja el a timert (waiting_for_task),
    // azt a lenti feliratkozás teszi meg.
  }

  // 2. Feliratkozás. FONTOS a sorrend: előbb ez, csak utána a konfiguráció.
  //    Az esp_task_wdt_reconfigure() a végén csak akkor indítja újra a timert,
  //    ha a figyelt taskok listája nem üres. Fordított sorrendben a listánk épp
  //    üres lenne (az idle taskokat leiratkoztatjuk).
  if (initErr == ESP_OK) {
    enableLoopWDT();
  }

  // 3. Timeout és panic beállítása.
  const esp_err_t cfgErr = (initErr == ESP_OK) ? esp_task_wdt_reconfigure(&cfg) : initErr;

  // 4. Az EGYETLEN megbízható visszajelzés. Az enableLoopWDT() void, a
  //    loopTaskWDTEnabled pedig a core belső változója - enélkül azt hinnénk,
  //    védve vagyunk, közben a feedWatchdog() csak ESP_ERR_NOT_FOUND-ot kapna,
  //    és 10 ms-onként egy log_e() sort öntene a soros portra.
  watchdogEnabled = (esp_task_wdt_status(NULL) == ESP_OK);

  if (!watchdogEnabled || cfgErr != ESP_OK) {
    // MINDEN hibaág ide fut be, egyetlen, jól kereshető figyelmeztetéssel.
    // Ne hazudjunk védelmet. Egy 5 mp-es alapértelmezett timeout ráadásul
    // rosszabb lenne a semminél: a 15-33 mp-ig tartó HTTP teszt alatt
    // újraindítana (a 33 mp-es esetet lásd a WDT_TIMEOUT_MS-nél).
    if (watchdogEnabled) {
      // Feliratkozva maradni a be nem állított (tipikusan 5 mp-es) timeouttal
      // pontosan a fenti csapda lenne: a http.GET() etetés nélküli blokkolása
      // alatt MINDEN teszt watchdog-panicba és újraindítási hurokba futna,
      // három kör után végzetes hibáig. Ezért kiiratkozunk: védelem nincs,
      // de a program legalább fut.
      disableLoopWDT();
      watchdogEnabled = (esp_task_wdt_status(NULL) == ESP_OK);
    }
    Serial.print("FIGYELEM: a watchdog NEM vedi a loop()-ot (feliratkozas ");
    Serial.print(watchdogEnabled ? "OK" : "SIKERTELEN/VISSZAVONVA");
    Serial.print(", hibakod ");
    Serial.print((int)cfgErr);
    Serial.println("). A program fut, de lefagyas eseten nem indul ujra.");
    return;
  }
  Serial.print("Watchdog enabled, timeout ");
  Serial.print(WDT_TIMEOUT_MS / 1000);
  Serial.println(" s");
}

// Csak akkor etetünk, ha a loop task már fel van iratkozva.
// A ket megszakitas-kezelo. IRAM_ATTR: a flash epp foglalt lehet (SPI olvasas),
// ezert az ISR nem elhet flashben. Mindketto CSAK jelzoket allit - semmi mas.
// A millis() ISR-bol is hivhato: a core-ban esp_timer_get_time()-ra epul, ami
// IRAM-ban van es megszakitasbol is ervenyes.
void IRAM_ATTR onResetButtonEdge() {
  if (digitalRead(resetPin) == LOW) {
    // A 0 a "nincs folyamatban levo lenyomas" jelolese; ha a millis() eppen 0
    // (korbefordulas), 1-re kerekitunk - ugyanaz a sentinel-kezeles, mint a
    // pollozott agban. Enelkul a korbefordulas pillanataban indult nyomas
    // sosem reteszelne.
    const uint32_t now = millis();
    btnResetDownAt = (now != 0) ? now : 1;
    return;
  }
  // Felfuto el: csak akkor reteszelunk, ha a lenyomas TELJES ERTEKU volt.
  const uint32_t down = btnResetDownAt;
  btnResetDownAt = 0;
  if (down != 0 && millis() - down >= BUTTON_DEBOUNCE_MS) {
    btnResetLatched = true;
  }
}

void IRAM_ATTR onWifiResetButtonEdge() {
  if (digitalRead(wifiresetPin) == LOW) {
    const uint32_t now = millis();      // lasd a 0 sentinelt az onResetButtonEdge()-ben
    btnWifiResetDownAt = (now != 0) ? now : 1;
    return;
  }
  const uint32_t down = btnWifiResetDownAt;
  btnWifiResetDownAt = 0;
  if (down != 0 && millis() - down >= BUTTON_DEBOUNCE_MS) {
    btnWifiResetLatched = true;
  }
}

// A reteszek elesitese. A setup()-ban a beragadt gomb ellenorzese UTAN hivjuk:
// egy beragadt gomb ugyis csak lefuto elt adna, felfutot sosem, tehat nem
// reteszelne - de a sorrend igy is egyertelmubb.
void armButtonLatches() {
  attachInterrupt(digitalPinToInterrupt(resetPin), onResetButtonEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(wifiresetPin), onWifiResetButtonEdge, CHANGE);
}

void feedWatchdog() {
  if (watchdogEnabled) {
    feedLoopWDT();
  }
}

void blockingDelay(uint32_t duration) {
  const uint32_t start = millis();
  while (millis() - start < duration) {
    const uint32_t elapsed = millis() - start;
    const uint32_t left = duration - elapsed;
    delay(left > BUTTON_POLL_MS ? BUTTON_POLL_MS : left);
    feedWatchdog();
  }
}

// Várakozás úgy, hogy a fizikai gombok közben is működnek
void waitWithButtons(uint32_t duration) {
  const uint32_t start = millis();
  while (millis() - start < duration) {
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
}

// Vissza van-e minden? Két lépés, ebben a sorrendben - és a SORREND a lényeg:
//   1. Wi-Fi: van-e kapcsolat? Ha nincs, csak KEZDEMÉNYEZÜNK egyet (aszinkron,
//      nem várunk rá), és kilépünk. Ez kísérlet, nem teszt: a kudarcnak nincs
//      következménye.
//   2. Internet: CSAK ha az 1. lépés szerint van kapcsolat, pingelünk egy fix
//      IP-t. Hálózat nélkül ennek nem lenne értelme, ezért el sem indul.
// Csak akkor igaz, ha MINDKETTŐ sikerül. Ezt kizárólag a hosszú várakozások
// korai lezárására használjuk.
//
// MIÉRT PING, amikor az internetteszt szándékosan HTTP? Mert itt más a tét.
// A ciklikus tesztnél egy hamis pozitív VÉGZETES lenne: befagyott router-DNS
// mellett a ping megy, és az eszköz sosem indítaná újra a routert (mérve: 41
// bukott HTTP teszt mellett nulla reset). Itt viszont a ping legrosszabb
// esetben annyit ér el, hogy a várakozás korábban ér véget - utána AZONNAL a
// rendes HTTP tesztsorozat következik, ami a befagyott DNS-t így is elbukja.
// Vagyis a hamis pozitív itt nem elrejt egy hibát, hanem hamarabb deríti ki.
// Cserébe a ping olcsó: nincs névfeloldás, nincs TCP, kevés rádióidő.
//
// FLASH: ez a függvény semmit nem ír a fájlrendszerre, és a WiFi.begin() sem
// ír NVS-be, mert a setup() WiFi.persistent(false)-t hívott. Az esemény-napló
// RTC RAM-ban van. Tehát tetszőleges gyakorisággal ismételhető.
// KET TASK, EGY MEMORIA. A programban ket task fut: a loop task es az
// AsyncTCP webszerver "async_tcp" taskja. Az alabbi globalisokhoz mindketto
// hozzafer - ezert erdemes egy helyen kimondani, mi vedi oket:
//
//   apDeadline              az async task irja (touchApDeadline, mind a 4
//                           kezelo), a loop olvassa. volatile, 32 bites
//                           igazitott szo - a C3-on egyetlen utasitas, nem
//                           szakadhat ketté.
//   restartPending/restartAt  ugyanez, volatile.
//   savingConfig            spinlock (beginConfigWrite) - ez a KONFIGZAR.
//   rtcEvents/rtcEvNext     evLogMux kritikus szakasz mindket iranyban.
//   rtcWdtResets            az async task CSAK OLVASSA (a /log lapon), a loop
//   rtcRetryRounds          irja. Igazitott szo, tehat nem szakadhat ketté; a
//                           lapon legfeljebb egy pillanattal regi ertek all -
//                           diagnosztikai kijelzon ez nem szamit.
//   ssid/pass/ipStr/gatewayStr   lasd alább.
//
// A NEGY KONFIGURACIOS PUFFERT az async task IRJA (a POST kezelo 2. fazisa),
// a loop task pedig OLVASSA - a WiFi.begin() hivasaiban. Ezt NEM zar vedi,
// hanem egy szerkezeti invarians:
//
//   Az initWiFi() es az onlineProbe() CSAK olyan helyekrol fut, ahol a
//   beallito portal nem letezik.
//
// Miert all ez? A portalt egyedul a startConfigPortal() inditja, az pedig
// deviceMode = MODE_CONFIG-ot allit, es a server.begin() az UTOLSO sora. A
// loop() a MODE_CONFIG (es a MODE_FATAL) againak elejen visszater - egyik ag
// sem er el sem initWiFi()-ig, sem onlineProbe()-ig. Visszafele ut nincs:
// MODE_CONFIG-bol csak ujraindulassal vagy MODE_FATAL-ba lehet kilepni, es
// MODE_MONITOR-ba egyik sem vezet vissza. A setup() ket MODE_MONITOR
// ertekadasa a portal letrejotte ELOTT van.
//
// Ket kezelo egymassal sem versenyez: az ESPAsyncWebServer MINDEN kezelot
// ugyanazon az async_tcp taskon, sorosan hiv - tehat a GET urlap (ami olvassa
// az ssid-t) es a POST (ami irja) sem futhat egyszerre.
//
// HA EZ VALAHA MEGVALTOZNA - barmi, ami a portal futasa kozben hivna
// initWiFi()-t vagy onlineProbe()-ot -, a negy puffert zar ala kell tenni.
// (Merve: CC1, CC2.)
bool onlineProbe() {
  // 1. LÉPÉS - Wi-Fi. Ez NEM teszt, hanem CSATLAKOZÁSI KÍSÉRLET, és nincs
  // következménye: ha nem sikerül, semmilyen számláló nem nő, semmilyen
  // állapot nem változik, és a hívó egyszerűen várakozik tovább. (A "3 próba,
  // aztán router reset" eszkalációhoz ennek semmi köze - az a firstStartDelay
  // LEJÁRTA után, a reconnectWifi()-ben kezdődik.)
  if (WiFi.status() != WL_CONNECTED) {
    // Nem várunk rá és nem blokkolunk: egy aszinkron újracsatlakozást
    // kezdeményezünk, a választ a KÖVETKEZŐ próba status()-a adja meg. A
    // hívások közt egy teljes ONLINE_PROBE_INTERVAL_MS telik, ami sokszorosa
    // egy asszociáció idejének - így nem szakítunk félbe egy futó próbát.
    // FONTOS INVARIANS: itt NINCS WiFi.mode() és NINCS WiFi.config(). Ez azért
    // helyes, mert a próba csak olyan helyekről fut, ahol a netif konfigurációja
    // ÉRINTETLEN az utolsó initWiFi() óta - a WiFi.disconnect(true) ugyanis
    // eldobná a statikus IP/DNS beállítást, és a begin() csendben DHCP-vel
    // jönne vissza. A sketchben mindössze két disconnect(true) van: az egyik
    // az enterDeepSleep()-ben (utána újraindulás), a másik a FAILURE_STATE
    // ágán, közvetlenül egy reconnectWifi() ELŐTT, ami újra alkalmazza a
    // konfigot. Ha valaha új disconnect(true) kerülne egy VÁRAKOZÁS elé, ezt
    // a függvényt is ki kell egészíteni. (Regresszió: WF10.)
    if (ssid[0] != '\0') {
      WiFi.begin(ssid, pass);
    }
    // ITT VISSZATÉRÜNK: pingelni ilyenkor értelmetlen (és pazarlás) lenne,
    // hiszen hálózat nélkül a csomag el sem indulna. A 2. lépés tehát CSAK
    // meglévő kapcsolat mellett fut - ezt az OP4 teszt őrzi.
    return false;
  }

  // 2. LÉPÉS - internet. Csak idáig eljutva van értelme: a kapcsolat megvan,
  // a kérdés már csak az, hogy kifelé is van-e út.
  const IPAddress target(PROBE_PING_IP[0], PROBE_PING_IP[1],
                         PROBE_PING_IP[2], PROBE_PING_IP[3]);
  return testInternetPing(target, "internet");
}

// Az onlineProbe() időzített változata: legfeljebb ONLINE_PROBE_INTERVAL_MS
// -enként enged tényleges próbát, közte azonnal hamissal tér vissza. Így a
// hívó minden körben hívhatja, a rádió mégsem dolgozik feleslegesen.
// A lastProbe-ot a hívó tartja, mert minden várakozásnak saját órája van.
bool onlineProbeDue(uint32_t& lastProbe, uint32_t now) {
  if (now - lastProbe < ONLINE_PROBE_INTERVAL_MS) {
    return false;
  }
  lastProbe = now;
  return onlineProbe();
}

// Ugyanaz, mint a waitWithButtons(), de közben figyeli, hogy visszajött-e a
// hálózat és az internet. Igazzal tér vissza, ha emiatt lépett ki korábban;
// hamissal, ha a teljes időt kivárta.
bool waitWithButtonsUntilOnline(uint32_t duration) {
  const uint32_t start = millis();
  uint32_t lastProbe = start;   // az első próba egy teljes intervallum múlva
  while (millis() - start < duration) {
    if (onlineProbeDue(lastProbe, millis())) {
      printUptime();
      Serial.println("A varakozas korabban veget er: halozat es internet OK.");
      return true;
    }
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  return false;
}

// Fájlírás közben SEM aludni, SEM újraindulni nem szabad: a félbeszakadt
// mentés sérült konfigurációt hagyna hátra. A mentést az aszinkron webszerver
// taskja végzi, az alvásról és az újraindításról viszont a loop() dönt -
// ezért itt megvárjuk, amíg az írás befejeződik.
//
// Korlátos: ha a jelző bármiért beragadna, az eszköz nem fagyhat le miatta.
// Egy konfigmentés 4 fájl írása visszaolvasásos ellenőrzéssel, ami tipikusan
// néhány tíz ezredmásodperc - az 5 másodperc bőven elég tartalék.
constexpr uint32_t SAVE_WAIT_MAX_MS = 5000;

// A konfigfájloknak egyszerre csak EGY írója lehet: vagy a webes mentés
// (async_tcp task), vagy a wifireset gomb törlése (loop task). A zár maga a
// savingConfig jelző - a megszerzését viszont atomikussá kell tenni, mert a
// puszta "if (savingConfig)" ellenőrzés és az írás megkezdése között a másik
// task közbeléphetne, és a két író ugyanazokat a fájlokat írná egyszerre.
// A rövid kritikus szakaszhoz ugyanazt a spinlockot használjuk, mint a napló.
bool beginConfigWrite() {
  bool acquired = false;
  portENTER_CRITICAL(&evLogMux);
  if (!savingConfig) {
    savingConfig = true;
    acquired = true;
  }
  portEXIT_CRITICAL(&evLogMux);
  return acquired;
}

// Az alvas es az ujraindulas kozos torlopontja: megvarja a folyamatban levo
// fajlirast, ES MEG IS SZEREZI a zarat.
//
// MIERT NEM ELEG MEGVARNI? A puszta megvaras ugy ter vissza, hogy a zar
// SZABAD. A visszateres es a tenyleges esp_deep_sleep_start() / ESP.restart()
// kozott viszont meg lefut ket-harom println es egy Serial.flush() - ez valos
// ezredmasodpercek -, es abban az ablakban az async_tcp task ELINDITHAT egy uj
// mentest. Pontosan azt a felbevagott fajlirast kapnank, ami ellen a varakozas
// egyaltalan van. A halasztott ujraindulas komment sajat maga mondja ki, hogy
// a mobilos DUPLA KOPPINTAS gyakori - tehat epp ilyen sorozat all elo.
// Ugyanaz a hiba volt a restartFromButton()-ben is; ott a beginConfigWrite()
// atomikus megszerzese oldotta meg, itt ugyanaz a megoldas. (Merve: SH1.)
//
// A zarat NEM oldjuk fel: aki ezt hivja, mar nem ter vissza. A hataridos
// kilepes viszont megmarad - egy beragadt jelzo miatt az eszkoz nem fagyhat le,
// es 5 mp utan a zar nelkul is tovabblepunk (a regi viselkedes).
void lockConfigBeforeShutdown() {
  if (beginConfigWrite()) {
    return;  // szabad volt, es most mar a mienk
  }
  printUptime();
  Serial.println("Fajliras folyik - megvarjuk, mielott alszunk vagy ujraindulunk.");
  const uint32_t start = millis();
  bool acquired = false;
  while (millis() - start < SAVE_WAIT_MAX_MS) {
    feedWatchdog();
    delay(BUTTON_POLL_MS);
    if (beginConfigWrite()) {
      acquired = true;
      break;
    }
  }
  if (acquired) {
    Serial.println("A fajliras befejezodott.");
  } else {
    Serial.println("FIGYELEM: a mentes 5 mp alatt sem fejezodott be, tovabblepunk.");
  }
}

// HTML-escape egy attribútumérték számára.
//
// MIÉRT KELL: az SSID tetszőleges 32 bájt lehet - idézőjelet, `<`-t és `&`-et
// is tartalmazhat. Escape NÉLKÜL az előkitöltés maga nyitna biztonsági rést a
// saját portálunkon: egy idézőjel kitörne a value="..." attribútumból, egy
// <script> pedig a lapba kerülne. A mentett SSID ráadásul a POST kezelőn át
// bármi lehet, tehát ez nem elméleti.
void printHtmlEscaped(AsyncResponseStream* r, const char* s) {
  for (const char* p = s; *p != '\0'; p++) {
    switch (*p) {
      case '&':  r->print(F("&amp;"));  break;
      case '<':  r->print(F("&lt;"));   break;
      case '>':  r->print(F("&gt;"));   break;
      case '"':  r->print(F("&quot;")); break;
      case '\'': r->print(F("&#39;"));  break;
      default:   r->write((uint8_t)*p); break;
    }
  }
}

// A beállító űrlap kiszolgálása, a mentett értékekkel ELŐKITÖLTVE.
//
// MIÉRT ELŐKITÖLTVE? Mert az üres címmező TÖRLÉST jelent. Aki statikus IP-vel
// üzemel és csak a jelszót akarja átírni, annak a böngésző üresen küldené a
// cím mezőket - és a mentés csendben DHCP-re váltana. (Mérve: AP1.)
//
// A JELSZÓT SOHA NEM töltjük elő: az az egyetlen titok ezen a lapon, és a
// portál WPA2 kulcsa nyilvános, tehát a lap tartalma nem tekinthető védettnek.
void sendConfigForm(AsyncWebServerRequest* request) {
  AsyncResponseStream* r = request->beginResponseStream("text/html", 1024);
  r->print(FORM_HEAD);

  r->print(F("SSID <input name=\"ssid\" maxlength=\"32\" required value=\""));
  printHtmlEscaped(r, ssid);
  r->print(F("\"><br>"));

  // A jelszó mező ÜRESEN indul, és a következménye ki van írva: enélkül a
  // felhasználó azt hinné, hogy a mentett jelszó megmarad.
  r->print(F("Password <input name=\"pass\" type=\"password\" maxlength=\"63\">"
             " <small>(ures = nyilt halozat)</small><br>"));

  r->print(F("IP <input name=\"ip\" maxlength=\"15\" placeholder=\"opcionalis\" value=\""));
  printHtmlEscaped(r, ipStr);
  r->print(F("\"><br>"));

  r->print(F("Gateway <input name=\"gateway\" maxlength=\"15\" placeholder=\"opcionalis\" value=\""));
  printHtmlEscaped(r, gatewayStr);
  r->print(F("\"><br>"));

  r->print(FORM_TAIL);
  request->send(r);
}

// A relé lábának rögzítése az alvás idejére. Deep sleep alatt a digitális
// padek (GPIO6-21) alapból nagyimpedanciásak; a gpio_hold_en() a pad
// pillanatnyi (LOW) kimenetét rögzíti, a gpio_deep_sleep_hold_en() pedig
// érvényben tartja a holdot deep sleep alatt is (C3: driver/gpio.h, a
// gpio_hold_en 3. megjegyzése). A router így alvás közben akkor sem veszít
// áramot, ha a külső lehúzó ellenállás hiányzik. A 22k lehúzó ettől még
// kell: a hold a BEKAPCSOLÁS és a program indulása közti ablakban nem él.
// A hold az ébredés (ami a C3-on reset) után is tart; a setup() oldja fel,
// miután a lábat már maga hajtja LOW-ra - a relé így ébredéskor sem "villan".
void holdRelayForSleep() {
  if (gpio_hold_en((gpio_num_t)relayPin) == ESP_OK) {
    gpio_deep_sleep_hold_en();
  } else {
    // Nem végzetes: a dokumentált külső lehúzó ellenállás a tartalék.
    Serial.println("FIGYELEM: a rele lab rogzitese (gpio_hold_en) nem sikerult.");
  }
}

// Igaz, ha a napló LEGUTOLSÓ bejegyzése már ez a kód. A sorozatok elleni
// spam-védelem közös alapja: a 32 bejegyzéses körpuffer néhány másodperc
// alatt kiszorítaná a kivizsgálandó eseményeket (BOOT, ROUTER RESET, FATAL),
// ha egy ismétlődő állapot minden körben új sort írna.
//
// A mux ugyanazért kell, mint a logEvent()-ben: a naplóba az async_tcp task
// is ír (a POST kezelő CONFIG_SAVED sora), tehát a pozíció és a slot együtt
// nem olvasható atomian.
bool lastEventWas(uint8_t code) {
  bool egyezik = false;
  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic == EVLOG_MAGIC && rtcEvNext > 0) {
    egyezik = (rtcEvents[(rtcEvNext - 1) % EVLOG_SIZE].code == code);
  }
  portEXIT_CRITICAL(&evLogMux);
  return egyezik;
}

// Igaz, ha a napló legutóbbi két bejegyzése már pontosan ez a beragadt-gomb
// kör (BOOT, majd STUCK BUTTON ugyanazzal a gombbal) - vagyis a mostani
// ébredés csak a 60 mp-es alvás-ébredés kör ismétlése.
bool stuckCycleAlreadyLogged(uint16_t which) {
  if (rtcEvMagic != EVLOG_MAGIC || rtcEvNext < 2) {
    return false;
  }
  const EventEntry& last = rtcEvents[(rtcEvNext - 1) % EVLOG_SIZE];
  const EventEntry& prev = rtcEvents[(rtcEvNext - 2) % EVLOG_SIZE];
  return last.code == EV_STUCK_BUTTON && last.param == which
         && prev.code == EV_BOOT;
}

// Szándékosan nem az enterDeepSleep()-et hívja: itt a Wi-Fi és a webszerver
// még el sem indult, és gombébresztést sem szabad armolni - a beragadt gomb
// azonnal újraébresztené az eszközt, azaz végtelen boot loop lenne.
// logIt = false: a beragadt-gomb kör ismétlése, nem kerül újra a naplóba
// (lásd a setup() spam-védelmét).
// Melyik gomb van lenyomva ÉPPEN MOST? 0 = reset, 1 = wifireset, -1 = egyik
// sem. A sorrend számít: ha valahogy mindkettő nyomva van, a reset gombot
// jelentjük - az kap gombébresztést, tehát az a veszélyesebb boot loop.
int pressedButtonNow() {
  if (digitalRead(resetPin) == LOW) return 0;
  if (digitalRead(wifiresetPin) == LOW) return 1;
  return -1;
}

void handleStuckButton(const char* message, uint16_t which, bool logIt) {
  Serial.println(message);
  Serial.print("Alvas ");
  Serial.print((unsigned long)(STUCK_BUTTON_SLEEP_US / 1000000ULL));
  Serial.println(" masodpercre, utana ujraprobalkozas.");
  Serial.flush();
  if (logIt) {
    logEvent(EV_STUCK_BUTTON, which);
  }

  // A két LED FELVÁLTVA villog. Ez szándékosan más, mint a végzetes hiba
  // jelzése (ott egyszerre villognak), így ránézésre megkülönböztethető.
  const uint32_t start = millis();
  bool on = false;
  while (millis() - start < STUCK_BLINK_MS) {
    on = !on;
    digitalWrite(ledPin, on ? HIGH : LOW);
    digitalWrite(wifiledPin, on ? LOW : HIGH);  // ellentétes fázis
    delay(FATAL_BLINK_MS);
  }
  digitalWrite(ledPin, LOW);
  digitalWrite(wifiledPin, LOW);

  // A relé itt is LOW (a setup() elején kapcsoltuk), és alvás alatt is az marad.
  holdRelayForSleep();
  // A LEZÁRÁS A LEGVÉGÉN, ahogy az enterDeepSleep()-ben is. A fenti
  // Serial.flush() a villogás ELŐTT állt, a holdRelayForSleep() viszont a
  // villogás UTÁN fut - és az KI TUD ÍRNI egy figyelmeztetést, ha a relé
  // lábának rögzítése nem sikerült. Flush nélkül épp az a sor veszne el a
  // deep sleepben, amiért az ember a soros portot nézi. (Mérve: SER7.)
  Serial.flush();
  Serial.end();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(STUCK_BUTTON_SLEEP_US);
  esp_deep_sleep_start();
}

void printUptime() {
  const uint32_t totalSec = (uint32_t)(esp_timer_get_time() / 1000000);
  const uint32_t d = totalSec / 86400;
  const uint32_t h = (totalSec % 86400) / 3600;
  const uint32_t m = (totalSec % 3600) / 60;
  const uint32_t s = totalSec % 60;

  char buf[48];
  if (d > 0) {
    snprintf(buf, sizeof(buf), "Uptime: %lud %luh %lum %lus",
             (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
  } else {
    snprintf(buf, sizeof(buf), "Uptime: %luh %lum %lus",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
  }
  Serial.println(buf);
}

// Initialize WiFi
bool initWiFi() {
  printUptime();
  if (ssid[0] == '\0') {
    Serial.println("Undefined SSID!");
    return false;
  }

  // Ha időközben (pl. az ESP saját auto-reconnectje miatt) már él a kapcsolat,
  // ne indítsuk újra: a WiFi.begin() feleslegesen lebontaná.
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected, skipping reconnect.");
    return true;
  }

  // A módot a config() előtt kell beállítani, különben egyes core verziókban
  // az "STA Failed to configure" hibával elszáll.
  WiFi.mode(WIFI_STA);

  // Statikus IP csak akkor, ha meg is adták. Üres mező = DHCP, nem hiba.
  bool staticOk = false;
  IPAddress localIP;
  IPAddress localGateway;
  if (ipStr[0] != '\0' || gatewayStr[0] != '\0') {
    // Régebbi firmware IPv6 címet is elmenthetett: az ilyet itt is ki kell
    // szűrni, különben a config() gateway és DNS nélküli statikus IP-t állítana.
    const bool ipValid = localIP.fromString(ipStr) && isUsableIPv4(localIP);
    const bool gatewayValid = localGateway.fromString(gatewayStr) && isUsableIPv4(localGateway);
    if (!ipValid) {
      Serial.println("❌ Invalid IP format (csak IPv4 hasznalhato)!");
    }
    if (!gatewayValid) {
      Serial.println("❌ Invalid gateway format (csak IPv4 hasznalhato)!");
    }
    staticOk = ipValid && gatewayValid;
  }

  if (staticOk) {
    // DNS-t is meg kell adni: statikus konfignál a DHCP-s DNS elveszik,
    // enélkül a névfeloldás (és így a HTTP teszt) mindig elbukna.
    const IPAddress subnet(255, 255, 255, 0);
    const IPAddress dnsFallback(1, 1, 1, 1);  // ha a gateway nem szolgál ki DNS-t
    if (!WiFi.config(localIP, localGateway, subnet, localGateway, dnsFallback)) {
      Serial.println("⚠️ STA Failed to configure");
      staticConfigActive = false;  // DHCP-re esünk vissza
    } else {
      Serial.println("✅ Manual IP config applied.");
      staticConfigActive = true;
    }
  } else {
    staticConfigActive = false;
    Serial.println("➡️ Skipping manual IP config. Using DHCP...");
  }

  WiFi.begin(ssid, pass);
  Serial.println("Connecting to WiFi...");
  Serial.print("Trying to connect to SSID: ");
  Serial.println(ssid);

  const uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
    if (millis() - startAttempt >= interval) {
      printUptime();
      Serial.println("Failed to connect.");
      return false;
    }
  }

  printUptime();
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  // Most van halozat: elinditjuk az oraszinkront. Nem varunk ra - a valasz a
  // hatterben erkezik, addig a naplo epoch mezoje 0 marad.
  startNtp();
  return true;
}

bool reset_device() {
  if (testState.resetStep == 0) {
    testState.resetEvents++;
    if (testState.resetEvents >= maxfailureEvents) {
      internetFailSleep();
    }
    logEvent(EV_ROUTER_RESET, (uint16_t)testState.resetEvents);
    // A naplo kiirasa a fajlrendszerre, MIELOTT a relehez nyulnank. Ha a
    // router ujrainditasa kozben aramszunet jon (nem ritka: epp azert
    // piszkaljuk a halozatot, mert valami nem stimmel), az RTC naplo elveszne
    // - a fajl viszont megmarad. A mentes NEM blokkolja a resetet: ha nem
    // sikerul, csak szolunk rola.
    saveEventLog("router reset elott");
    Serial.println("Router resetting");
    Serial.print("Powering OFF the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, HIGH);
    Serial.println("Relay on.");
    // A státusz LED a pulzus alatt VILLOG (lásd lent), nem csak kialszik:
    // 90 mp sötét LED ránézésre nem különböztethető meg a halott eszköztől.
    // A villogás azt mondja: "épp én kapcsoltam le a routert, dolgozom".
    digitalWrite(ledPin, LOW);
    timing.blinkLast = millis();
    uiFlags.blinkOn = false;
    // A Wi-Fi LED viszont KI marad: a router most áram nélkül van, tehát
    // kapcsolat sincs. Enélkül a LED a teljes pulzus + RESET_DELAY alatt
    // (~11,5 perc) azt mutatná, hogy van Wi-Fi. Visszakapcsolni a sikeres
    // újracsatlakozás fogja.
    digitalWrite(wifiledPin, LOW);
    Serial.println("Reset_pulse delay.");
    testState.resetStep = 1;
    timing.resetPulseStart = millis();
    // A pulzus most indult: ebben a hívásban még biztosan nem járt le.
    return false;
  }

  if (millis() - timing.resetPulseStart >= RESET_PULSE) {
    Serial.println("Reset_pulse delay end.");
    Serial.print("Powering ON the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, LOW);
    digitalWrite(ledPin, HIGH);  // a villogás vége: a jelzés visszaáll folyamatosra
    printUptime();
    testState.resetStep = 0;
    return true;
  }

  // A pulzus alatt: a státusz LED villog 2 Hz-cel. A hívó ciklusa
  // BUTTON_POLL_MS-enként hív minket, tehát ez elég sűrű ütem hozzá.
  if (millis() - timing.blinkLast >= RESET_BLINK_MS) {
    timing.blinkLast = millis();
    uiFlags.blinkOn = !uiFlags.blinkOn;
    digitalWrite(ledPin, uiFlags.blinkOn ? HIGH : LOW);
  }
  return false;
}

// FIGYELEM (hardver): deep sleep alatt az ESP32-C3 digitális lábai (GPIO6-21)
// alapból nagyimpedanciás állapotba kerülnek. A relé lábát (D10 = GPIO10)
// ezért alvás előtt a holdRelayForSleep() rögzíti LOW-ra (gpio_hold_en +
// gpio_deep_sleep_hold_en) - így az alvás alatt sem lebeg. A relé
// vezérlőbemenetére ettől függetlenül TOVÁBBRA IS kell külső lehúzó ellenállás
// (a jelenlegi bekötésben 22k a GND felé): a hold a bekapcsolás és a program
// indulása közti ablakban még nem él, és a szoftveres védelem hibája ellen
// is ez az utolsó háló.
// Közös elalvás. timerUs = 0 esetén NINCS időzített ébresztés: az eszköz
// magától nem tér vissza, csak a reset gombra vagy áramtalanításra.
void enterDeepSleep(uint64_t timerUs) {
  // Egyetlen torlópont MINDEN alvásra (apSleep, internetFailSleep,
  // retrySleep, fatalSleep): fájlírás közben nem alszunk el.
  lockConfigBeforeShutdown();
  digitalWrite(ledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(wifiledPin, LOW);
  // A meghajtott LOW rögzítése az alvás idejére - a sorrend kötött: előbb a
  // digitalWrite, aztán a hold, mert a hold a pad PILLANATNYI állapotát fogja.
  holdRelayForSleep();
  WiFi.disconnect(true);
  server.end();
  Serial.flush();
  Serial.end();

  // A gomb-megszakítások leválasztása MÉG az ébresztőforrások élesítése előtt.
  //
  // MIÉRT? Az attachInterrupt() és az esp_deep_sleep_enable_gpio_wakeup()
  // UGYANAZOKAT a GPIO megszakítás-regisztereket állítja. A futásidejű
  // "bármelyik él" beállítás és az alváshoz kért "LOW szint" ébresztés
  // egymásra hatása nem dokumentált - ezért nem hagyatkozunk rá: előbb
  // leválasztunk, aztán élesítünk. Ugyanaz az elv, mint egy sorral lejjebb a
  // "tiszta lappal indulunk"-nál. Ébredés után a setup() újra élesíti őket.
  detachInterrupt(digitalPinToInterrupt(resetPin));
  detachInterrupt(digitalPinToInterrupt(wifiresetPin));

  // Tiszta lappal indulunk, hogy biztosan csak az legyen élesítve, amit akarunk.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  esp_err_t gpioErr = ESP_FAIL;
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  // A reset gomb ébressze fel az eszközt. Csak RTC-képes láb használható
  // (ESP32-C3: GPIO0-GPIO5); a resetPin a XIAO ESP32-C3-on D1 = GPIO3.
  // Szándékosan NINCS itt a wifireset gomb, és a handleStuckButton() sem
  // armol gombébresztést: egy beragadt gomb így nem tud boot loopot okozni.
  // Az IDF 6.0 átnevezte ezt az API-t, az arduino-esp32 pedig idf ">=5.3,<6.2"
  // tartományt deklarál, tehát mindkét névvel találkozhatunk.
#if ESP_IDF_VERSION_MAJOR >= 6
  gpioErr = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << resetPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  gpioErr = esp_deep_sleep_enable_gpio_wakeup(1ULL << resetPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
#endif

  // Biztonsági háló: ébresztőforrás nélkül az eszköz külső resetig aludna.
  // Ha nem kértünk időzítőt ÉS a gombébresztés armolása nem sikerült (pl. mert
  // valaki nem RTC-képes lábra tette a gombot), inkább mégis armolunk egy
  // hosszú időzítőt, mint hogy az eszköz elérhetetlenné váljon.
  if (timerUs == 0 && gpioErr != ESP_OK) {
    timerUs = SLEEP_DURATION_US;
  }
  if (timerUs > 0) {
    esp_sleep_enable_timer_wakeup(timerUs);
  }
  esp_deep_sleep_start();
}

// Az internet tartósan nem jön vissza a router újraindításai után sem.
// A Wi-Fi ilyenkor működik, csak a kapcsolat rossz a szolgáltató felé, ezért
// van értelme később magától újrapróbálni - ez az EGYETLEN időzített alvás.
void internetFailSleep() {
  printUptime();
  Serial.print("A router ujrainditasa ");
  Serial.print(maxfailureEvents - 1);
  Serial.println(" alkalommal sem hozta vissza az internetet.");
  Serial.print("Alvas ");
  Serial.print((unsigned long)(SLEEP_DURATION_US / 60000000ULL));
  Serial.println(" percre, utana automatikus ujraprobalkozas.");
  logEvent(EV_SLEEP, 2);
  // Egy ora alvas kovetkezik. Ha kozben aramszunet lesz, az RTC naplo
  // elveszik - ezert a fajlba is kiirjuk, mielott elalszunk.
  saveEventLog("tartos internetkieses, alvas elott");
  enterDeepSleep(SLEEP_DURATION_US);
}

// A hálózat nem látszik. Lehet, hogy a router fagyott le - pontosan erre való
// ez az eszköz. Áramtalanítjuk, kivárjuk a bootolást, majd újra próbálkozunk.
// Ugyanaz a menet, mint a működés közbeni kapcsolatvesztésnél.
bool routerResetAndRetry() {
  printUptime();
  Serial.println("A halozat nem latszik - router ujrainditas kovetkezik.");
  while (!reset_device()) {
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  printUptime();
  Serial.println("Router reset kesz, varakozas a bootolasra.");
  // Ha a router hamarabb feláll, nem várjuk ki a maradékot. A próba a
  // kapcsolatot már igazolta, tehát a reconnectWifi() is felesleges.
  if (waitWithButtonsUntilOnline(RESET_DELAY)) {
    digitalWrite(wifiledPin, HIGH);
    return true;
  }
  return reconnectWifi();
}

// Hitelesítési hiba? Ilyenkor a router újraindítása értelmetlen.
bool wifiAuthFailed() {
  return WiFi.status() == WL_CONNECT_FAILED;
}

// A hálózat nincs ott, de valószínűleg visszajön: alvás egy órát, majd új kör.
void retrySleep() {
  printUptime();
  Serial.print("A halozat nem elerheto. Ujraprobalkozasi kor: ");
  Serial.print(rtcRetryRounds);
  Serial.print(" / ");
  Serial.println(MAX_RETRY_ROUNDS);
  Serial.println("Alvas 1 orara, utana automatikus ujraprobalkozas.");
  logEvent(EV_SLEEP, 1);
  saveEventLog("ujraprobalkozasi alvas elott");
  enterDeepSleep(SLEEP_DURATION_US);
}

// Nem sikerult csatlakozni a 3 probaval sem. Itt dol el, hogy tovabb varunk-e
// vagy beallito modba megyunk.
//
// A core meg tudja kulonboztetni a ket esetet (STA.cpp:146-148):
// WIFI_REASON_NO_AP_FOUND -> WL_NO_SSID_AVAIL (a halozat nem is latszik),
// WIFI_REASON_AUTH_FAIL -> WL_CONNECT_FAILED (rossz jelszo).
//
// FONTOS: az AUTH_FAIL agnak van egy "&& !first_connect" feltetele is, es a
// first_connect (STA.cpp:117) fuggveny-szintu static, ami csak az ELSO
// disconnect utan valt false-ra. Az elso sikertelen tarsitas tehat meg
// WL_DISCONNECTED-et ad. Ezert nem eleg egyetlen probalkozas: a setup() egy
// probaja utan a handleFirstStart() meg harmat tesz, es a dontes csak azutan
// szuletik meg. Regresszio: R1 (a probalkozasok szamat is meri) es R5.
//
// Konzervativan dontunk: CSAK az explicit hitelesitesi hiba kuld AP modba. Minden mas esetben ujraprobalkozunk, mert
// egy teves "varjunk tovabb" ara kesleltetett ujrakonfiguralas, a teves
// "menjunk AP modba" ara viszont egy halott eszkoz.
void wifiGiveUp() {
  const wl_status_t st = WiFi.status();
  printUptime();
  Serial.print("WiFi status a probalkozasok utan: ");
  Serial.println((int)st);

  if (st == WL_CONNECT_FAILED) {
    Serial.println("Hitelesitesi hiba - valoszinuleg rossz a jelszo. AP mod.");
    logEvent(EV_AP_MODE, 2);
    startConfigPortal();
    return;
  }

  rtcRetryRounds++;
  if (rtcRetryRounds >= MAX_RETRY_ROUNDS) {
    Serial.println("Ket napja nincs halozat - AP beallito mod.");
    rtcRetryRounds = 0;
    logEvent(EV_AP_MODE, 3);
    startConfigPortal();
    return;
  }
  retrySleep();
}

// Az AP beállító mód visszaszámlálásának újraindítása. Minden HTTP kérésnél
// meghívjuk, így ha a felhasználó az utolsó pillanatban nyitja meg az oldalt,
// kap még egy teljes időablakot a kitöltésre.
void touchApDeadline() {
  apDeadline = millis() + AP_TIMEOUT_MS;
}

// AP beállító mód lejárt mentés nélkül. Ugyanaz a logika, mint a LittleFS
// hibánál: időzített ébresztés nincs, csak a reset gomb hozza vissza.
void apSleep() {
  printUptime();
  Serial.println("Az AP beallito modban nem tortent mentes, az ESP elalszik.");
  Serial.println("Idozitett ebresztes NINCS - reset gomb vagy aramtalanitas kell.");
  logEvent(EV_SLEEP, 3);
  enterDeepSleep(0);
}

// Végzetes hiba után elalvás. Időzített ébresztés NINCS: a hiba magától nem
// múlik el, ezért értelmetlen lenne óránként felébredni és újra villogni.
void fatalSleep() {
  printUptime();
  Serial.println("5 perc hibajelzes utan az ESP elalszik.");
  Serial.println("Idozitett ebresztes NINCS - reset gomb vagy aramtalanitas kell.");
  logEvent(EV_SLEEP, 4);
  enterDeepSleep(0);
}

// Végzetes hiba egy BLOKKOLÓ környezetből (a gombkezelőkből).
//
// Az enterFatal() csak beállítja a módot, a jelzést pedig a loop() végzi. A
// gombkezelők viszont mély blokkoló ciklusokból is futnak - a
// waitWithButtonsUntilOnline(RESET_DELAY) például akár 10 percig nem ad
// vissza a loop()-nak.
// Addig az eszköz vidáman tovább működne: tesztelne, relét kapcsolna, aludna.
// Ezért itt helyben, blokkolva jelzünk, és SOHA nem térünk vissza - pontosan
// úgy, ahogy az eddigi ESP.restart() sem tért vissza ebből az ágból.
void fatalHalt(const char* reason) {
  enterFatal(reason);  // mód, relé LOW (a router kapjon áramot), üzenetek
  // Az enterFatal() altalanos uzenete a loop()-vezerelt esetre igaz, ahol
  // mindket gomb el. Itt csak a reset gomb - ezt ki kell mondani.
  Serial.println("FIGYELEM: itt a wifireset gomb NEM hat, csak a reset gomb.");

  while (millis() - timing.fatalStart < FATAL_SLEEP_AFTER_MS) {
    const uint32_t now = millis();
    if (now - timing.blinkLast >= FATAL_BLINK_MS) {
      timing.blinkLast = now;
      uiFlags.blinkOn = !uiFlags.blinkOn;
      digitalWrite(ledPin, uiFlags.blinkOn ? HIGH : LOW);
      digitalWrite(wifiledPin, uiFlags.blinkOn ? HIGH : LOW);
    }
    // Csak a reset gomb él. A wifiresetbutton()-t szándékosan NEM hívjuk:
    // épp onnan jöhettünk, az önmagába vezető rekurzió lenne.
    resetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  fatalSleep();  // időzített ébresztés NÉLKÜL - nem tér vissza
}

// A gombos újraindítás közös útja - a pollozott és a reteszelt ág is ezt hívja.
//
// A zár ATOMIKUS megszerzése: a hívó gyors savingConfig-ellenőrzése és az
// ESP.restart() között az async_tcp taskban ELINDULHAT egy webes mentés, és a
// köztük lévő két println() + Serial.flush() nem is elhanyagolható idő. Az
// újraindítás ilyenkor félbevágná a fájlírást, és csonka konfigurációt hagyna
// a flashben. A zárral ez kizárt: ha megkaptuk, mentés már nem indulhat; ha
// nem kaptuk meg, visszatérünk, és a következő 10 ms-os kör újra próbálkozik.
// Feloldani nem kell - a zár az újraindulással hal el.
void restartFromButton(const char* reason) {
  if (!beginConfigWrite()) {
    return;
  }
  Serial.println(reason);
  Serial.println("RESTART ESP32C3 device.");
  Serial.flush();
  ESP.restart();
}

void resetbutton() {
  // Fájlírás közben SEMMIKÉPP nem indítunk újra: a félbeszakadt mentés sérült
  // konfigurációt hagyna hátra. Ugyanaz a szabály, mint az elalvásnál.
  // A mentés alatt a debounce sem indul el, tehát utána egy teljes 50 ms-os
  // lenyomás kell - egy mentés néhány tíz ezredmásodperc, ez nem érzékelhető.
  if (savingConfig) {
    return;
  }
  // A megszakitas-alapu retesz: egy TELJES ERTEKU (>= BUTTON_DEBOUNCE_MS)
  // lenyomas, ami egy blokkolo szakasz alatt tortent, es a gombot azota
  // felengedtek. A debounce-t nem kerulte meg - a hosszat az ISR merte meg.
  //
  // A jelzot SZANDEKOSAN NEM toroljuk: a restartFromButton() vagy ujraindit
  // (es akkor a RAM-mal egyutt a jelzo is eltunik), vagy visszater, mert a
  // konfigzar epp foglalt - ilyenkor a retesznek MEG KELL MARADNIA. A gomb
  // ekkor mar fel van engedve, tehat a pollozott ag nem tudna potolni: a
  // jelzo torlese a felhasznalo gombnyomasat NYOM NELKUL eldobna.
  if (btnResetLatched) {
    timing.resetBtnDownSince = 0;
    restartFromButton("Reset button pressed (latched).");
    return;  // idaig csak akkor jutunk, ha a zar foglalt volt
  }

  if (digitalRead(resetPin) != LOW) {
    timing.resetBtnDownSince = 0;  // felengedve: debounce újraindul
    return;
  }
  const uint32_t now = millis();
  if (timing.resetBtnDownSince == 0) {
    // A 0 a "felengedve" jelölés; ha a millis() épp 0 (körbefordulás), 1-re
    // kerekítünk, hogy ne ütközzön vele. Hatása legfeljebb 1 ms a debounce-ban.
    timing.resetBtnDownSince = (now != 0) ? now : 1;
    return;
  }
  // Csak akkor fogadjuk el, ha végig lenyomva maradt (valódi debounce)
  if (now - timing.resetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    restartFromButton("Reset button pressed.");
  }
}

// A wifireset közös útja - a pollozott és a reteszelt ág is ezt hívja.
// A zárat itt is atomikusan szerezzük meg (lásd restartFromButton()).
void doWifiReset() {
  if (!beginConfigWrite()) {
    return;  // épp mentés folyik; a következő kör újra próbálja
  }
  Serial.println("WIFIRESET button is pulling down!");
  Serial.println("RESET saved wifi data!");
  // FONTOS a sorrend: az SSID megy ELŐSZÖR. A "wifireset" célja, hogy az
  // eszköz a beállító portálon jöjjön fel, és ezt egyedül a /ssid.txt dönti
  // el (setup(): ha üres az SSID -> AP mód). Ha a törlés közben elmegy az
  // áram, így a legvalószínűbb, hogy a kívánt végállapotba kerülünk.
  // A &= szándékosan nem rövidzáras: mind a négy törlés lefut akkor is, ha
  // valamelyik elbukik.
  bool cleared = true;
  cleared &= clearConfigValue(LittleFS, ssidPath);
  cleared &= clearConfigValue(LittleFS, passPath);
  cleared &= clearConfigValue(LittleFS, ipPath);
  cleared &= clearConfigValue(LittleFS, gatewayPath);
  if (!cleared) {
    // Ha a fájlrendszer nem írható, az eszköz NEM működhet tovább: a
    // konfiguráció mentése ugyanígy elbukna, az újraindítás pedig a régi
    // adatokkal jönne fel - a gomb a felhasználó szemszögéből "nem csinál
    // semmit". Ez ugyanaz a hibaosztály, mint a többi LittleFS hiba, tehát
    // ugyanaz a kezelés: mindkét LED gyorsan villog, 5 perc múlva alvás,
    // amiből csak a gomb vagy az áramtalanítás hoz vissza.
    Serial.println("!!! A mentett wifi adatok törlése NEM sikerült !!!");
    logEvent(EV_FATAL, 4);
    // A zár oldása még a hibajelzés előtt: a fatalHalt() gombkezelőjét a
    // beragadt savingConfig némává tenné (a resetbutton() ellenőrzi).
    savingConfig = false;
    fatalHalt("A mentett wifi adatok nem torolhetok - serult fajlrendszer.");
    // fatalHalt() nem tér vissza
  }
  Serial.println("RESTART ESP32C3 device.");
  Serial.flush();
  ESP.restart();  // a zár az újraindulással hal el
}

void wifiresetbutton() {
  // Mentés közben a törlés és az újraindítás is végzetes lenne: két task írná
  // egyszerre ugyanazokat a fájlokat. Ez itt csak a GYORS kapu; az igazi
  // védelem a doWifiReset()-ben lévő beginConfigWrite().
  if (savingConfig) {
    return;
  }
  // Megszakítás-alapú retesz - lásd a resetbutton() megfelelő ágát, beleértve
  // azt is, hogy a jelzőt NEM töröljük: ha a zár foglalt, a nyomás megmarad.
  if (btnWifiResetLatched) {
    timing.wifiResetBtnDownSince = 0;
    doWifiReset();
    return;
  }
  if (digitalRead(wifiresetPin) != LOW) {
    timing.wifiResetBtnDownSince = 0;
    return;
  }
  const uint32_t now = millis();
  if (timing.wifiResetBtnDownSince == 0) {
    // Lásd a resetbutton() megjegyzését a 0 sentinelről.
    timing.wifiResetBtnDownSince = (now != 0) ? now : 1;
    return;
  }
  if (now - timing.wifiResetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    doWifiReset();
  }
}

bool reconnectWifi() {
  printUptime();
  Serial.println("Starting reconnectWifi loop");

  for (uint8_t attempt = 0; attempt < wifi_maxRetries; attempt++) {
    printUptime();
    Serial.print("Attempt ");
    Serial.println(attempt + 1);

    if (initWiFi()) {
      printUptime();
      Serial.println("WIFI RECONNECTED!");
      digitalWrite(wifiledPin, HIGH);
      return true;
    }

    printUptime();
    Serial.print("WIFI ERROR! WiFi status: ");
    Serial.println(WiFi.status());

    if (attempt + 1 < wifi_maxRetries) {
      printUptime();
      Serial.print(wifiInterval / 1000);
      Serial.println(" seconds delay start.");
      waitWithButtons(wifiInterval);  // gombok közben is élnek, nincs busy-loop
    }
  }

  printUptime();
  Serial.print("WIFI FAILED TO RECONNECT AFTER ");
  Serial.print(wifi_maxRetries);
  Serial.println(" attempts!");
  // A folytatásról a hívó dönt (mindenhol: AP beállító mód).
  return false;
}

// Egy bájt, legfeljebb timeoutMs várakozással. -1: lejárt a határidő, vagy a
// szerver lezárta a kapcsolatot. Ez a kettő az egyetlen kilépési ok - a hívó
// mindkettőt "nincs több adat"-ként kezeli.
int readByteBounded(WiFiClient& stream, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    // Csak akkor olvasunk, ha VAN mit: a read() a socket saját fogadási
    // timeoutját használja, ami nem a mienk. Ha vakon hívnánk, egyetlen
    // olvasás túlléphetné a timeoutMs határidőt - így viszont a határidő
    // valóban a miénk, és nem függ a core beállításaitól.
    if (stream.available() > 0) {
      return stream.read();  // <0 is lehet: available() ígért, de elszállt
    }
    // A szerver lezárta és nincs több adat: nincs értelme a timeoutot kivárni
    if (!stream.connected()) {
      return -1;
    }
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    // delay() és nem yield(): a yield() csak azonos prioritású taskok között
    // ad át vezérlést, tehát üresen pörgetné a CPU-t a válaszra várva.
    delay(BUTTON_POLL_MS);
  }
  return -1;
}

// Korlátozott méretű, időzáras olvasás: nem allokál, és nem tud "elszállni"
// egy captive portal többszáz kilobájtos válaszán. A timeoutMs bájtok KÖZÖTTI
// határidő, tehát egy lassan csordogáló válasz is végigolvasható, egy néma
// kapcsolat viszont nem tart fel tovább egy timeoutnál.
size_t readBounded(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  while (n < maxLen) {
    const int c = readByteBounded(stream, timeoutMs);
    if (c < 0) {
      break;
    }
    buf[n++] = (char)c;
  }
  return n;
}

// Egy hexa számjegy értéke, vagy -1. NAGYBETŰT IS elfogad, mert a chunked
// keretezés méret-sorai az RFC 9112 szerint bármelyik alakban jöhetnek.
//
// Ne keverd a hexVal()-lal: az a MI saját jelszó-kódolásunkat olvassa vissza,
// és szándékosan csak kisbetűset fogad el (azt írjuk ki). A két függvény
// neve korábban csak egy betűben tért el, a viselkedésük viszont nem -
// ezért kapott ez beszédesebb nevet.
int hexDigitAnyCase(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Ugyanaz, mint a readBounded(), de lebontja a chunked keretezést.
//
// MIÉRT KELL: a HTTPClient a darabhatárokat CSAK a getString() /
// writeToStream() útján bontja le - azokat viszont nem használjuk, mert
// korlátlanul foglalnának. A nyers streamben tehát benne maradnak a
// keretbájtok ("7\r\nsuccess\r\n0\r\n\r\n"), és a strcmp() a tökéletesen
// működő végpontot is bukottnak látná. Content-Length-et küldő végpontnál ez
// sosem jön elő, de egy közbeiktatott proxy bármikor átkeretezheti a választ.
size_t readChunked(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  for (uint16_t chunk = 0; chunk < HTTP_MAX_CHUNKS; chunk++) {
    // Méret sor: hexa szám, opcionális ";kiterjesztés", CRLF.
    uint32_t size = 0;
    bool sawDigit = false;
    bool inExt = false;
    for (;;) {
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      if (c == '\n') break;
      if (c == '\r' || inExt) continue;
      if (c == ';') { inExt = true; continue; }
      const int d = hexDigitAnyCase(c);
      // Nem hexa a méret helyén: ez nem chunked keret. Nem találgatunk,
      // a teszt elbukik - ez a biztonságos irány.
      if (d < 0) return n;
      if (size > (0xFFFFFFFFu - (uint32_t)d) / 16u) return n;  // túlcsordulás
      size = size * 16u + (uint32_t)d;
      sawDigit = true;
    }
    if (!sawDigit || size == 0) {
      return n;  // üres méret sor, vagy a lezáró 0-s darab
    }
    for (uint32_t i = 0; i < size; i++) {
      if (n >= maxLen) {
        return n;  // ekkora választ nem a várt végpont küld - nem olvassuk végig
      }
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      buf[n++] = (char)c;
    }
    // A darabot lezáró CRLF: elnyeljük, de nem kötjük meg magunkat a pontos
    // alakjában - a következő kör úgyis hexát vár.
    for (uint8_t i = 0; i < 2; i++) {
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      if (c == '\n') break;
    }
  }
  return n;
}

bool testInternetHTTP(const char* url, const char* expectedResponse) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout((int32_t)HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout((uint16_t)HTTP_RESPONSE_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    Serial.println("Error: HTTP begin failed");
    return false;
  }

  // A _transferEncoding privat, a nyers fejlec viszont igy elkerheto. Ez a
  // hivas a VALASZ fejleceire vonatkozik (a HTTPClient.h kommentje felrevezeto,
  // a handleHeaderResponse() tolti fel oket).
  static const char* kCollectHeaders[] = { "Transfer-Encoding" };
  http.collectHeaders(kCollectHeaders, 1);

  const int httpCode = http.GET();
  bool result = false;

  if (expectedResponse[0] == '\0') {
    // "generate_204" stilusu vegpont: nincs torzs, csak a statuszkod szamit.
    // Ez SZIGORUBB, mint a szoveg-egyeztetes: egy captive portal nem tud 204-et
    // adni, mert neki eppenseggel HTML-t vagy atiranyitast KELL kuldenie.
    // Ugyanezt a dontest hozza a NetworkManager is (nm-connectivity.c: 204 ->
    // "no content, as expected"; barmi mas -> portal).
    result = (httpCode == HTTP_CODE_NO_CONTENT);
    // Sikernel NEM irunk semmit: azt a hivo "Successful Test" sora mondja ki.
    // A soros portra csak az kerul ki, ami hibakeresesnel szamit.
    if (!result) {
      Serial.print("Error on HTTP request, code: ");
      Serial.println(httpCode);
    }
  } else if (httpCode == HTTP_CODE_OK) {
    // Chunked valasznal a getSize() -1, es a nyers streamben ott vannak a
    // keretbajtok is - azokat le kell bontani, kulonben a jo valasz is bukik.
    const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    const int len = http.getSize();
    if (len > (int)HTTP_MAX_PAYLOAD) {
      // Ekkora választ nem a várt endpoint küld (pl. captive portal)
      Serial.print("Unexpected payload size: ");
      Serial.println(len);
    } else {
      // len == 0 (Content-Length: 0): nincs mit olvasni, várni sem kell rá.
      // len < 0: nincs Content-Length, a keretet a kapcsolat zárása adja.
      const size_t want = (len >= 0) ? (size_t)len : HTTP_MAX_PAYLOAD;
      char payload[HTTP_MAX_PAYLOAD + 1];
      // Ugyanaz a stream, amit a http.begin() kapott — nem függünk a
      // getStream() core-verziónként eltérő visszatérési típusától.
      const size_t n = chunked
                         ? readChunked(client, payload, HTTP_MAX_PAYLOAD, HTTP_READ_TIMEOUT_MS)
                         : readBounded(client, payload, want, HTTP_READ_TIMEOUT_MS);
      payload[n] = '\0';
      trimInPlace(payload);  // a záró CR/LF ne buktassa el az egyezést
      result = (strcmp(payload, expectedResponse) == 0);
      // Csak eltéréskor beszélünk. Ilyenkor viszont a KAPOTT törzs a
      // legfontosabb információ: abból derül ki, hogy captive portál ült-e
      // a kérésre, vagy az üzemeltető változtatta meg a választ.
      if (!result) {
        Serial.println(payload);
        Serial.println("Hamis érték!");
      }
    }
  } else {
    Serial.print("Error on HTTP request, code: ");
    Serial.println(httpCode);
  }

  http.end();
  return result;
}

bool testInternetPing(const IPAddress& target, const char* targetName) {
  Serial.print("Ping teszt futtatása (");
  Serial.print(targetName);
  Serial.print(" - ");
  Serial.print(target);
  Serial.println(")...");
  uint8_t successCount = 0;

  for (uint8_t j = 0; j < PING_ATTEMPTS; j++) {
    // A Ping.ping() érték szerint veszi a címet (ESPping: bool ping(IPAddress,
    // int16_t)), tehát a másolatot ő maga készíti - nem kell külön helyi példány.
    const bool pingOK = Ping.ping(target, 1);  // 1 próbálkozás pingenként
    Serial.print("Ping ");
    Serial.print(j + 1);
    if (pingOK) {
      Serial.println(" sikeres.");
      successCount++;
      // Az eredmény eldőlt, a maradék pinget felesleges megvárni
      if (successCount >= PING_MIN_SUCCESS) {
        Serial.println("✅ Ping teszt sikeres.");
        return true;
      }
    } else {
      Serial.println(" sikertelen.");
      if (j == 0) {
        Serial.println("⚠️ Első ping hiba — lehet, hogy a hálózat ébred.");
      }
      const uint8_t remaining = PING_ATTEMPTS - (j + 1);
      if (successCount + remaining < PING_MIN_SUCCESS) {
        Serial.println("❌ Ping teszt sikertelen — hálózati probléma valószínű.");
        return false;
      }
    }
    if (j + 1 < PING_ATTEMPTS) {
      waitWithButtons(PING_GAP_MS);  // Kíméletes tesztelés
    }
  }

  // Ide nem lehet eljutni: 4 probaval es 2-es kuszobbel a ciklus mindig a ket
  // korai return valamelyiken lep ki (a j=2 koron a 0 sikeres mar elbukott, a
  // j=3-on a 2. siker mar visszateres). A fordito viszont megkoveteli, ezert
  // ez a sor a lefedettsegben mindig fehér marad.
  return successCount >= PING_MIN_SUCCESS;
}

// Elérhető-e a saját gateway-ünk?
//
// Csak statikus IP mellett van értelme: DHCP-nél a gateway magától a routertől
// jött. Ha viszont kézzel adtad meg és elgépelted, akkor a Wi-Fi TÁRSÍTÁS
// sikerül (az WPA2, 2. réteg), de IP szinten nincs út sehová - és a router
// újraindítása ezen soha nem segít. Épp ezt a különbséget méri ez a teszt:
// a hozzáférési pont él (különben nem lennénk csatlakozva), de nem érjük el.
//
// FIGYELEM: ha a routered nem válaszol ICMP echóra, ez tévesen "elérhetetlen"-t
// ad. Ezért is kap a router előbb egy esélyt, és csak a második alkalommal
// megyünk AP módba. Ha a routered blokkolja a pinget, használj DHCP-t.
bool gatewayUnreachable() {
  if (!staticConfigActive || gatewayStr[0] == '\0') {
    return false;  // nincs mit ellenőrizni
  }
  IPAddress gw;
  // Vedelmi ag, amit a gyakorlatban nem lehet elerni: a staticConfigActive csak
  // akkor igaz, ha az initWiFi()-ben a gatewayStr mar sikeresen ertelmezodott,
  // es azt csak a POST kezelo irja at - az viszont ujraindit. Emiatt ez a sor
  // sem szerepel a lefedettsegben.
  if (!gw.fromString(gatewayStr) || !isUsableIPv4(gw)) {
    return false;
  }
  return !testInternetPing(gw, "sajat gateway");
}

void handleFirstStart(uint32_t currentMillis) {
  // A várakozás korai lezárása. Az órát a setup() állítja be úgy, hogy az ELSŐ
  // próba azonnal esedékes legyen: a tipikus eset ugyanis az, hogy a kapcsolat
  // MÁR él (a setup() initWiFi()-je sikerült), és ilyenkor semmi értelme
  // 60 mp-et várni a továbblépéssel.
  //
  // FIGYELEM, VISELKEDÉSVÁLTOZÁS: korábban a puszta Wi-Fi kapcsolat is azonnal
  // lezárta a várakozást. Most a ping is kell hozzá.
  //
  // Ez NEM azt jelenti, hogy internet híján mindig letelik a 10 perc: a próba
  // 60 mp-enként ISMÉTLŐDIK, tehát ha az internet a várakozás bármely pontján
  // visszajön, a kilépés az azt követő ütemben megtörténik (mérve: OP7 - a
  // 3,5 percnél visszatérő internetnél a várakozás 4 perc alatt lezárult).
  // A teljes 10 perc csak akkor telik le, ha VÉGIG nincs internet - és épp
  // ez a helyes: áramszünet után a router lassabban indul, mint az ESP, és
  // nem akarjuk 2 perccel a bekapcsolás után újraindítani.
  const bool online = onlineProbeDue(firstStartProbeLast, currentMillis);

  if (!online) {
    if (currentMillis - timing.startMillis < firstStartDelay) {
      if (!uiFlags.firstStartPrinted) {
        printUptime();
        Serial.println("First start wait begin.");
        uiFlags.firstStartPrinted = true;
      }
      resetbutton();
      wifiresetbutton();
      feedWatchdog();
      delay(BUTTON_POLL_MS);  // vTaskDelay: 10 percig ne pörgesse a CPU-t
      return;  // csak itt kilép, visszaadja a vezérlést a loop()-nak
    }

    printUptime();
    Serial.println("First start wait end.");

    if (!reconnectWifi()) {
      // Rossz jelszónál a router újraindítása értelmetlen - egyből AP mód.
      if (wifiAuthFailed()) {
        wifiGiveUp();
        return;
      }
      // Egyébként: hátha a router fagyott le. Áramtalanítás, majd újra.
      if (!routerResetAndRetry()) {
        wifiGiveUp();
        return;
      }
    }
  } else {
    // A próba már igazolta a kapcsolatot: nincs mit újracsatlakoztatni.
    printUptime();
    Serial.println("First start wait end (halozat es internet visszajott).");
    digitalWrite(wifiledPin, HIGH);
  }
  // Innentől a firstStart lezárult. A timing.startMillis viszont NEM válik
  // érdektelenné: a watchdog számláló nullázása is ehhez méri az "1 óra
  // hibátlan működést" (loop(), WDT_COUNTER_CLEAR_MS). Ezért a mező végig a
  // BOOT időbélyege marad - frissíteni nemcsak felesleges, hanem hibás is
  // lenne, mert eltolná a watchdog-ablakot.
  uiFlags.firstStart = false;
}

void startConfigPortal() {
  if (deviceMode == MODE_CONFIG) {
    return;  // már fut
  }
  // Naplomentes MEG a modvaltas elott. Az AP mod azt jelenti, hogy az eszkoz
  // feladta a csatlakozast - epp ezt az elozmenyt akarja latni az, aki
  // odamegy es megnyitja a portalt. A mentes ugyanabban a pillanatban meg
  // MODE_MONITOR-ban tortenik, tehat a webszerver meg nem fut: a fajlirasnak
  // nincs versenytarsa a masik taskbol.
  saveEventLog("AP modba valtas elott");
  deviceMode = MODE_CONFIG;
  touchApDeadline();
  // Jelzés: a Wi-Fi LED villog (a villogtatást a loop() végzi), a státusz LED
  // közben végig világít. Az utóbbi azért kell expliciten, mert ide a router
  // reset ága felől is be lehet érkezni, ahol a státusz LED épp villogott.
  digitalWrite(wifiledPin, LOW);
  digitalWrite(ledPin, HIGH);
  timing.blinkLast = millis();
  uiFlags.blinkOn = false;

  Serial.println("Setting AP (Access Point)");
  char apName[32];
  snprintf(apName, sizeof(apName), "ESP-%s", ESP.getChipModel());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName, AP_PASSWORD);
  Serial.print("AP SSID: ");
  Serial.println(apName);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Web Server Root URL. Az űrlap a programból megy ki, nem a LittleFS-ről:
  // nincs se feltöltendő data/ mappa, se olyan eset, hogy a fájlrendszer
  // állapota miatt ne lenne beállító felület.
  //
  // A LittleFS-ről szándékosan NEM szolgálunk ki semmit: egy serveStatic("/")
  // a teljes fájlrendszert kiadta volna, azaz a /pass.txt-ben tárolt Wi-Fi
  // jelszót is.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    sendConfigForm(request);
  });
  // Keep-alive. A nyitva lévő oldal 60 mp-enként meghívja, így az AP mód
  // visszaszámlálása addig tolódik, amíg tényleg ott vagy a lapon. A válasz
  // szándékosan egyetlen bájt: percenként fut, és a rádió a legdrágább.
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    request->send(200, "text/plain", "1");
  });

  // Diagnosztikai napló. Ez az egyetlen mód, hogy soros kábel nélkül megtudd,
  // mi történt az eszközzel - és épp AP módban vagy, amikor baj van.
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    // A stream puffere igény szerint nő (resizeAdd), de akkor soronként
    // újraallokálna. Egy bőséges kezdőmérettel ez egyetlen foglalás lesz:
    // fejléc + állapot + 32 sor x ~70 bájt + a ~900 bájtos jelmagyarázat +
    // lábléc alatta marad.
    AsyncResponseStream* r = request->beginResponseStream("text/html", 6144);
    r->print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
               "<title>Naplo</title></head><body><h2>Diagnosztikai naplo</h2>"));

    const esp_reset_reason_t rr = esp_reset_reason();
    r->printf("<p><b>Utolso indulas oka:</b> %s (%d)<br>",
              resetReasonName(rr), (int)rr);
    r->printf("<b>Watchdog ujraindulasok:</b> %u / %u<br>",
              (unsigned)rtcWdtResets, (unsigned)MAX_WDT_RESETS);
    r->printf("<b>Ujraprobalkozasi korok:</b> %u / %u<br>",
              (unsigned)rtcRetryRounds, (unsigned)MAX_RETRY_ROUNDS);
    // Az uptime ugyanabban az alakban, mint a soros porton - a nyers
    // masodpercbol (pl. "165600 mp") ranezesre semmi nem latszik.
    {
      const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
      r->printf("<b>Uptime:</b> %ud %uh %um %us</p>",
                (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600),
                (unsigned)((up % 3600) / 60), (unsigned)(up % 60));
    }

    // Pillanatkép a naplóról a mux alatt: az író (logEvent) a loop taskból
    // fut, ez a kezelő az async_tcp taskból - enélkül félig kiírt bejegyzést
    // is olvashatnánk. A kritikus szakasz egy memcpy.
    //
    // EGYETLEN puffert hasznalunk mindkét forráshoz (RTC és fájl): az
    // async_tcp task veremje véges, és 32 bejegyzés már 384 bájt. Előbb
    // eldöntjük, melyik forrás kell, és csak azt töltjük be.
    uint32_t evTotal;
    EventEntry evCopy[EVLOG_SIZE];
    portENTER_CRITICAL(&evLogMux);
    evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
    memcpy(evCopy, rtcEvents, sizeof(evCopy));
    portEXIT_CRITICAL(&evLogMux);

    // MELYIK A FRISSEBB? Az RTC naplo vagy a fajlba mentett?
    //
    // A fajl mindig az RTC naplo egy KORABBI pillanatkepe. Ebbol kovetkezik a
    // szabaly:
    //  - Ha az RTC naplo TULELTE a mentes ota eltelt idot (nem volt
    //    aramszunet), akkor bovebb is nala: mindent tartalmaz, ami a fajlban
    //    van, PLUSZ ami azota tortent. Ilyenkor az RTC nyer.
    //  - Ha az RTC naplot torolte egy aramszunet, a szamlalo nullarol indult,
    //    tehat a fajl tobb elozmenyt orzott meg. Ilyenkor a fajl nyer - es
    //    epp ez a mentes ertelme.
    //  - Ha viszont az RTC ota mar 32 UJ esemeny keletkezett, akkor a
    //    korpuffer teljesen tele van friss adattal, ami idoben mindenkeppen
    //    ujabb a fajlnal.
    //  - Ha MINDKETTONEK van valos ideje (NTP), az dont: a nagyobb idobelyeg
    //    nyer. Ez a legpontosabb valasz, ezert ez az elso szabaly.
    //
    // Ha epp fajliras folyik a masik taskbol, a fajlt NEM olvassuk: az RTC
    // naplo ilyenkor is ep, es ez az egyszeru kizaras eleg.
    // A dontes CSAK a fejlecbol tortenik, es a bejegyzeseket utana toltjuk be -
    // igy egyszerre csak EGY 384 bajtos puffer all a vermen.
    //
    // Ha epp fajliras folyik a masik taskbol, a fajlt NEM olvassuk. Ez a
    // kizaras nem atomi (az iras a kerdes utan is elindulhat), de nem is kell
    // annak lennie: egy felig kiirt fajlon a fejlec ellenorzese bukik, es a
    // lap ugyanoda jut - az RTC naplohoz.
    EvFileHeader fej;
    bool vanFajl = false;
    if (!savingConfig && loadEventLogHeader(fej)) {
      bool rtcNyer;
      if (evTotal == 0) {
        rtcNyer = false;                       // nincs mit mutatni az RTC-bol
      } else {
        // A fajl sajat idobelyege (mikor mentettuk) a helyes osszehasonlitasi
        // alap: pontosan azt mondja meg, mikori a tartalma.
        const uint32_t rtcEpoch = evCopy[(evTotal - 1) % EVLOG_SIZE].epoch;
        if (rtcEpoch >= NTP_MIN_VALID_EPOCH
            && fej.savedEpoch >= NTP_MIN_VALID_EPOCH) {
          rtcNyer = (rtcEpoch >= fej.savedEpoch);   // valos ido dont
        } else {
          rtcNyer = (evTotal >= fej.evNextAtSave) || (evTotal >= EVLOG_SIZE);
        }
      }
      if (!rtcNyer) {
        if (loadEventLogEntries(fej, evCopy)) {
          // A fajl bejegyzesei mar KIEGYENESITVE vannak (a legregebbitol a
          // legujabbig), es a puffer elejen allnak; az evTotal a darabszam
          // lesz, igy a lenti kiiro ciklus mindket forrasra ugyanaz.
          vanFajl = true;
          evTotal = fej.count;
        } else {
          // A BETOLTES FELUTON BUKOTT. Mivel EGY puffert hasznalunk, az
          // olvasas addigra mar felulirhatta az RTC pillanatkep elejet -
          // a puffer most fel fajl, fel RTC adat lenne. Ezert nem eleg
          // "visszalepni" az RTC-re: UJRA kell venni a pillanatkepet.
          portENTER_CRITICAL(&evLogMux);
          evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
          memcpy(evCopy, rtcEvents, sizeof(evCopy));
          portEXIT_CRITICAL(&evLogMux);
        }
      }
    }

    if (evTotal == 0) {
      // Sem az RTC-ben, sem a fajlban nincs semmi (vagy a fajl nem letezik,
      // ures, csonka). Ez nem hiba: egyszeruen nincs mit mutatni.
      r->print(F("<p>Nincs rogzitett esemeny.</p>"));
    } else {
      if (vanFajl) {
        char mikor[24];
        r->print(F("<p><b>Forras:</b> a fajlrendszerre mentett naplo"));
        if (formatEpoch(fej.savedEpoch, mikor, sizeof(mikor))) {
          r->printf(" (mentve: %s)", mikor);
        }
        r->print(F(" - az RTC naplo ennel regebbi vagy ures "
                   "(pl. aramszunet torolte).</p>"));
      } else {
        r->print(F("<p><b>Forras:</b> az RTC memoriaban levo naplo "
                   "(ez a frissebb).</p>"));
      }
      r->print(F("<table border=1 cellpadding=4><tr><th>Ido</th><th>Uptime</th>"
                 "<th>Esemeny</th><th>Param</th></tr>"));
      // A legregebbi meg meglevo bejegyzestol indulunk. Fajlbol olvasva a
      // bejegyzesek mar sorban allnak, tehat a "% EVLOG_SIZE" ott is helyes.
      const uint32_t shown = evTotal < EVLOG_SIZE ? evTotal : EVLOG_SIZE;
      for (uint32_t i = evTotal - shown; i < evTotal; i++) {
        const EventEntry& e = evCopy[i % EVLOG_SIZE];
        char mikor[24];
        r->print(F("<tr><td>"));
        if (formatEpoch(e.epoch, mikor, sizeof(mikor))) {
          r->print(mikor);
        } else {
          // Nem volt (meg) oraszinkron ennel a bejegyzesnel. Nem hiba - a
          // uptime oszlop ilyenkor is elmond mindent.
          r->print(F("-"));
        }
        r->printf("</td><td>%u:%02u:%02u</td><td>%s</td><td>%u</td></tr>",
                  (unsigned)(e.uptimeSec / 3600), (unsigned)((e.uptimeSec % 3600) / 60),
                  (unsigned)(e.uptimeSec % 60), eventName(e.code), (unsigned)e.param);
      }
      r->print(F("</table>"));
    }
    // Jelmagyarazat: a Param oszlop szamai kulonben csak a forraskodbol
    // fejthetok meg - epp az az informacio veszne el, amiert az oldal van.
    // FIGYELEM a jelolesre: az esemenyneveket SZANDEKOSAN nem tesszuk <b> koze.
    // A tablazat cellai ">NEV<" alaku szoveget adnak, es a naplo tartalmara
    // pontosan erre a mintara lehet illeszteni (igy teszi az L2 teszt is).
    // Egy <b>BOOT</b> ugyanezt a mintat allitana elo a jelmagyarazatban, es
    // elmosna a kulonbseget "a naplóban VAN ilyen esemeny" es "a lap emliti
    // ezt az esemenyt" kozott.
    r->print(F("<h3>Param jelentese</h3><ul>"
               "<li>BOOT: az indulas oka (lasd fent)</li>"
               "<li>WIFI OK: hanyadik ujraprobalkozasi korben sikerult</li>"
               "<li>WIFI LOST: a WiFi.status() erteke</li>"
               "<li>TEST FAIL: a bukott teszt sorszama (1-5)</li>"
               "<li>ROUTER RESET: hanyadik ujrainditas (1-4)</li>"
               "<li>AP MODE: 1 = nincs SSID, 2 = rossz jelszo, "
               "3 = letelt a 2 nap, 4 = a gateway sem erheto el</li>"
               "<li>GW UNREACH: 1 = a router reset elott, 2 = utana is</li>"
               "<li>SLEEP: 1 = ujraprobalkozas, 2 = tartos internetkieses, "
               "3 = AP idotullepes, 4 = vegzetes hiba</li>"
               "<li>FATAL: 1 = LittleFS, 2 = konfig olvasas, "
               "3 = watchdog, 4 = wifireset torles, 5 = tartosan keves heap</li>"
               "<li>WDT RESET: hanyadik rendellenes ujraindulas</li>"
               "<li>STUCK BUTTON: 0 = reset gomb, 1 = wifireset gomb</li>"
               "<li>LOW HEAP: a szabad heap KB-ban a kuszob atlepesekor</li>"
               "<li>HEAP RESTART: a szabad heap KB-ban az onkentes "
               "ujraindulas elott</li>"
               "</ul>"));
    r->print(F("<p><i>Az uptime minden indulaskor nullarol indul, ezert a "
               "BOOT sorok jelzik az ujraindulasokat. A naplo az "
               "aramtalanitast nem eli tul.</i></p>"
               "<p><a href=\"/\">Vissza a beallitasokhoz</a></p>"));
    // A naplót is lehet 5 percnél tovább olvasni - ne aludjon el közben.
    // Flashből megy a pufferbe, nem allokál külön.
    r->print(KEEPALIVE_JS);
    r->print(F("</body></html>"));
    request->send(r);
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    // A 404 is interakció: valaki épp az eszközzel dolgozik. A böngésző
    // magától kéri például a /favicon.ico-t, és egy elgépelt cím sem
    // jelenti azt, hogy a felhasználó elment. Enélkül a "minden HTTP kérés
    // kitolja a határidőt" szabály nem lenne igaz.
    touchApDeadline();
    request->send(404, "text/plain", "Not found");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    if (!fsReady) {
      // Nincs értelme menteni: a fájlrendszer nem áll rendelkezésre.
      request->send(500, "text/plain",
                    "LittleFS nem elerheto, a beallitasok nem menthetok. "
                    "Ellenorizd a particios semat (partitions_custom.csv, "
                    "'spiffs' cimkeju particio).");
      return;
    }

    // --- 1. FÁZIS: validálás. Itt még sem fájlt nem írunk, sem globálist nem
    // módosítunk: bármely mező hibája esetén MINDEN marad a régiben, így az
    // 500-as válasz ("nem mentettünk") szó szerint igaz. A korábbi, menet
    // közben író változat a hibás mező ELŐTT álló érvényes mezőket már
    // kiírta - egy rossz gateway így fél-új, fél-régi konfigurációt hagyott
    // a flashben, amivel az eszköz a következő induláskor rossz statikus
    // címen jött volna fel.
    bool saveOk = true;
    bool ssidProvided = false;
    // Az első hiba oka, hogy a felhasználó konkrét visszajelzést kapjon.
    const char* failReason = nullptr;

    // A jelöltek. A "Set" jelzők azt mondják meg, mely mezők érkeztek a
    // kérésben - ami nem érkezett, annak a mentett értéke marad érvényben.
    char ssidNew[SSID_MAX_LEN + 1] = { 0 };
    char passNew[PASS_MAX_LEN + 1] = { 0 };
    char encNew[SECRET_ENC_MAX + 1] = { 0 };
    char ipNew[IPSTR_MAX_LEN + 1] = { 0 };
    char gwNew[IPSTR_MAX_LEN + 1] = { 0 };
    bool passSet = false;
    bool ipSet = false;
    bool gwSet = false;

    const int params = request->params();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter* p = request->getParam(i);
      if (!p->isPost()) {
        continue;
      }
      const String& name = p->name();
      const String& val = p->value();  // referencia: nincs felesleges másolat

      if (name == PARAM_SSID) {
        // A readConfigValue() beolvasáskor levágja a whitespace-t, ezért már
        // itt is levágjuk: így az elmentett és a visszajelzett érték pontosan
        // az, amivel az eszköz később csatlakozni fog. A csupa szóközből álló
        // SSID így üresre fogy - azt pedig nem szabad sikerként elfogadni,
        // mert az újraindulás után AP módban kötnénk ki.
        char candidate[SSID_MAX_LEN + 1];
        strlcpy(candidate, val.c_str(), sizeof(candidate));
        trimInPlace(candidate);
        if (val.length() <= SSID_MAX_LEN && candidate[0] != '\0') {
          ssidProvided = true;
          strlcpy(ssidNew, candidate, sizeof(ssidNew));
        } else {
          Serial.println("Invalid SSID length!");
          saveOk = false;
          if (failReason == nullptr) failReason = "Ervenytelen SSID (1-32 karakter, nem csak szokoz).";
        }
      } else if (name == PARAM_PASS) {
        if (val.length() <= PASS_MAX_LEN) {
          strlcpy(passNew, val.c_str(), sizeof(passNew));
          // TUDATOS KORLAT: a vagas a masolas-beillesztessel bekerult szokozok
          // ellen szol, es a WPA2 szabvany szerint egy jelszo ELEJEN vagy VEGEN
          // allo szokoz onmagaban ervenyes volna. Ilyen jelszo ezzel az
          // eszkozzel NEM allithato be - a mentett ertek a vagott valtozat lesz.
          // Nem a tarolas kenyszeriti ki: a jelszo "v1:" + hexa alakban megy a
          // fajlba, abban nincs szokoz, tehat a kodolas/dekodolas a szokozt
          // hibatlanul visszaadna (ellentetben az SSID-vel, ami sima szovegkent
          // tarolodik, es amit a readConfigValue() beolvasaskor ugyis vag).
          trimInPlace(passNew);
          // Összekeverve mentjük, hogy egy flash dumpon a `strings` ne adjon
          // használható jelszót. A visszaolvasásos ellenőrzés érintetlen: a
          // writeConfigValue() a kódolt formát verifikálja.
          if (encodeSecret(passNew, encNew, sizeof(encNew))) {
            passSet = true;
          } else {
            Serial.println("- a jelszo kodolasa nem fert a pufferbe");
            saveOk = false;
            if (failReason == nullptr) failReason = "Belso hiba a jelszo mentesekor.";
          }
        } else {
          Serial.println("Password too long!");
          saveOk = false;
          if (failReason == nullptr) failReason = "A jelszo tul hosszu (max 63 karakter).";
        }
      } else if (name == PARAM_IP) {
        // Az SSID-hez es a jelszohoz hasonloan a masolas-beillesztes szokozeit
        // itt is levagjuk a validalas elott: a "192.168.1.200 " szandeka
        // egyertelmu. A puffer bovebb a mezonel, hogy a szokozokkel egyutt is
        // beferjen a vagas elott; a vagott ertek hosszat kulon ellenorizzuk.
        // A csupa szokoz uresre fogy = DHCP, nem hibauzenet.
        char candidate[IPSTR_MAX_LEN * 2 + 2];
        strlcpy(candidate, val.c_str(), sizeof(candidate));
        trimInPlace(candidate);
        IPAddress testIP;
        if (candidate[0] == '\0') {
          ipNew[0] = '\0';
          ipSet = true;
        } else if (val.length() < sizeof(candidate)
                   && strlen(candidate) <= IPSTR_MAX_LEN && testIP.fromString(candidate)
                   && isUsableIPv4(testIP)) {
          // A val.length() feltetel az SSID/jelszo agak explicit hossz-
          // ellenorzesenek parja: a strlcpy() csonkolasa utan egy tulmeretes
          // bemenet vege csendben elveszne, es a maradek akar ervenyes cimme
          // is vagodhatna - ilyet nem fogadunk el, az a hiba-agra tartozik.
          strlcpy(ipNew, candidate, sizeof(ipNew));
          ipSet = true;
        } else {
          Serial.println("Invalid IP format!");
          saveOk = false;
          if (failReason == nullptr) failReason = "Ervenytelen IP cim (csak IPv4, nem 0.0.0.0).";
        }
      } else if (name == PARAM_GATEWAY) {
        // Ugyanaz a vagas, mint az IP-nel.
        char candidate[IPSTR_MAX_LEN * 2 + 2];
        strlcpy(candidate, val.c_str(), sizeof(candidate));
        trimInPlace(candidate);
        IPAddress testIP;
        if (candidate[0] == '\0') {
          gwNew[0] = '\0';
          gwSet = true;
        } else if (val.length() < sizeof(candidate)
                   && strlen(candidate) <= IPSTR_MAX_LEN && testIP.fromString(candidate)
                   && isUsableIPv4(testIP)) {
          // Lasd az IP ag megjegyzeset a csonkolas elleni vedelemrol.
          strlcpy(gwNew, candidate, sizeof(gwNew));
          gwSet = true;
        } else {
          Serial.println("Invalid gateway format!");
          saveOk = false;
          if (failReason == nullptr) failReason = "Ervenytelen gateway (csak IPv4, nem 0.0.0.0).";
        }
      }

      // A jelszót soha nem írjuk ki nyíltan a soros portra
      if (name == PARAM_PASS) {
        Serial.printf("POST[%s]: <%u chars>\n", name.c_str(), (unsigned)val.length());
      } else {
        Serial.printf("POST[%s]: %s\n", name.c_str(), val.c_str());
      }
    }

    // SSID nélkül az eszköz nem tudna hova csatlakozni: ilyet ne fogadjunk el
    // sikerként, mert az újraindulás után ugyanitt kötnénk ki.
    if (!ssidProvided) {
      saveOk = false;
      if (failReason == nullptr) failReason = "Hianyzo SSID.";
    }

    // Statikus IP-hez a gateway is kell. Az initWiFi() csak akkor konfigurál,
    // ha MINDKETTŐ értelmezhető - félig kitöltve csendben DHCP-re esne vissza,
    // a felhasználó viszont azt olvasná, hogy a megadott fix címen lesz.
    // A VÉGSŐ értékpárt nézzük: a most küldöttet, különben a mentettet.
    const char* ipFinal = ipSet ? ipNew : ipStr;
    const char* gwFinal = gwSet ? gwNew : gatewayStr;
    if ((ipFinal[0] != '\0') != (gwFinal[0] != '\0')) {
      saveOk = false;
      if (failReason == nullptr) {
        failReason = "Statikus IP-hez az IP cimet ES a gateway-t is meg kell adni "
                     "(DHCP-hez hagyd mindkettot uresen).";
      }
    }

    if (!saveOk) {
      // Ne hazudjunk sikert és főleg ne indítsunk újra: az újraindítás
      // eldobná a beírt adatokat, a felhasználó pedig ugyanitt kötne ki.
      // Fájlt nem írtunk és globálist sem módosítottunk - minden a régi.
      touchApDeadline();
      Serial.println("A beallitasok mentese SIKERTELEN.");
      // A leghosszabb indoklás (statikus IP + gateway) a rögzített szöveggel
      // együtt 176 bájt; a snprintf() csonkolna, ha ennél kisebb lenne.
      char err[208];
      snprintf(err, sizeof(err),
               "A beallitasok mentese nem sikerult: %s "
               "Az eszkoz NEM indul ujra, probald meg ismet.",
               failReason ? failReason : "ismeretlen hiba.");
      request->send(500, "text/plain", err);
      return;
    }

    // --- 2. FÁZIS: minden mező érvényes -> zár, globálisok, fájlok.
    // A zár (savingConfig) atomikus megszerzése: a wifireset gomb törlése a
    // loop taskban ugyanezeket a fájlokat írná. Amíg a zár él, a loop() nem
    // altatja el az eszközt, és a gombok sem szólnak közbe.
    if (!beginConfigWrite()) {
      request->send(503, "text/plain",
                    "Az eszkoz eppen a konfiguraciot irja (gombos torles vagy "
                    "masik mentes). Probald ujra egy pillanat mulva.");
      return;
    }

    strlcpy(ssid, ssidNew, sizeof(ssid));
    Serial.print("SSID set to: ");
    Serial.println(ssid);
    saveOk &= writeConfigValue(LittleFS, ssidPath, ssid);

    if (passSet) {
      strlcpy(pass, passNew, sizeof(pass));
      Serial.print("Password set to: ");
      Serial.print((unsigned)strlen(pass));
      Serial.println(" chars");
      saveOk &= writeConfigValue(LittleFS, passPath, encNew);
    }
    if (ipSet) {
      strlcpy(ipStr, ipNew, sizeof(ipStr));
      if (ipStr[0] == '\0') {
        Serial.println("IP empty, using DHCP.");
      } else {
        Serial.print("IP Address set to: ");
        Serial.println(ipStr);
      }
      saveOk &= writeConfigValue(LittleFS, ipPath, ipStr);
    }
    if (gwSet) {
      strlcpy(gatewayStr, gwNew, sizeof(gatewayStr));
      if (gatewayStr[0] == '\0') {
        Serial.println("Gateway empty, using DHCP.");
      } else {
        Serial.print("Gateway set to: ");
        Serial.println(gatewayStr);
      }
      saveOk &= writeConfigValue(LittleFS, gatewayPath, gatewayStr);
    }

    savingConfig = false;
    touchApDeadline();

    if (!saveOk) {
      // Ide már csak tényleges fájlrendszer-hiba hozhat; ilyenkor a flash
      // tartalma lehet vegyes, de az eszköz nem indul újra, és a válasz
      // megmondja, mi történt.
      Serial.println("A beallitasok mentese SIKERTELEN.");
      request->send(500, "text/plain",
                    "A beallitasok mentese nem sikerult: LittleFS irasi hiba. "
                    "Az eszkoz NEM indul ujra, probald meg ismet.");
      return;
    }

    char message[96];
    if (ipStr[0] != '\0') {
      snprintf(message, sizeof(message),
               "Done. ESP will restart. Then go to IP address: %s", ipStr);
    } else {
      snprintf(message, sizeof(message),
               "Done. ESP will restart and connect to your router (DHCP).");
    }
    request->send(200, "text/plain", message);

    // Az async callbackben nem blokkolunk és nem indítunk újra:
    // a loop() teszi meg, miután a válasz kiment.
    logEvent(EV_CONFIG_SAVED, 0);
    restartAt = millis() + RESTART_GRACE_MS;
    restartPending = true;
  });

  server.begin();
}

void setup() {
  timing.startMillis = millis();
  // A first start próbájának órája egy teljes intervallummal "hátra" áll, hogy
  // az ELSŐ próba azonnal esedékes legyen. Az előjel nélküli kivonás
  // körbefordulása itt szándékos és helyes: a különbség akkor is pontosan egy
  // intervallum, ha a millis() még kicsi.
  firstStartProbeLast = timing.startMillis - ONLINE_PROBE_INTERVAL_MS;
  // Ugyanez a heap allapotsorara: az ELSO kiiras legyen azonnal esedekes, hogy
  // a bootolas utani heap-szint rogton lathato legyen. (Az "== 0 tehat meg
  // sosem irtunk" sentinel helyett ugyanaz a minta, mint a probak oraival.)
  heapLogLast = timing.startMillis - HEAP_LOG_INTERVAL_MS;

  pinMode(wifiresetPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(wifiledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(wifiledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(ledPin, HIGH);  //bekapcsolja a ledet, +5volt 150 ohm

  // Ha alvásból ébredtünk, a relé lábát még az alvás előtti hold tartja LOW-n
  // (lásd holdRelayForSleep()). Most már mi hajtjuk meg: előbb a meghajtás
  // (fent), aztán a feloldás - így a láb egy pillanatra sem marad magára.
  // Első bekapcsoláskor mindkét hívás ártalmatlan no-op.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)relayPin);

  Serial.begin(115200);
  const uint32_t serialTimeout = millis();
  while (!Serial && millis() - serialTimeout < 3000) { delay(BUTTON_POLL_MS); }
  blockingDelay(500);  // USB CDC beállása, hogy az induló logok ne vesszenek el

  printUptime();

  // Innentől figyeli a watchdog a programot - a setup() maradékát is.
  //
  // MIÉRT ITT? Amint a soros port használható (hogy a hibaüzenete kimenjen),
  // és minden más ELŐTT. Korábban a LittleFS csatolása után élesedett, mert a
  // begin(true) első indításkor FORMÁZ, és az etetés nélküli formázás egy
  // ~1,5 MB-os partíción 15-20 mp volt - az akkori sémával ez indokolt
  // kihagyás volt. A partitions_custom.csv 512 KiB-os "spiffs" partíciója
  // viszont csak 128 szektor: tipikusan 4-7 mp (30-50 ms/szektor), és még a
  // szélsőségesen lassú, 400 ms/szektoros esetben is ~51 mp - mindkettő bőven
  // a 90 mp-es WDT_TIMEOUT_MS alatt. Így a formázás már felügyelhető, és a
  // setup() eddig védtelen szakaszai (gombellenőrzés, csatolás, konfig
  // olvasás) is a watchdog alá kerülnek.
  //
  // A setup() etetés nélküli leghosszabb szakaszai innentől: a beragadt gomb
  // 3 mp-es villogása (utána deep sleep) és a fenti formázás. Az initWiFi()
  // 20 mp-es várakozása és minden blockingDelay() magától etet.
  //
  // A hardveres interrupt watchdog (ESP_INT_WDT, 300 ms) végig aktív, de az
  // csak a "kemény" megállást fogja meg (letiltott megszakítás, megállt tick).
  // A csendes, szabályosan blokkoló beragadást csak ez a task watchdog látja.
  initWatchdog();

  // Mindkét gombot ellenőrizzük: ha bármelyik beragadt, nem indulunk el.
  // Spam-védelem: a beragadt gomb 60 mp-es alvás-ébredés köre percenként adna
  // egy BOOT + STUCK BUTTON párt, ami fél óra alatt kiszorítaná a körpufferből
  // a kivizsgálandó eseményeket. Ezért csak az ELSŐ kör kerül a naplóba, az
  // ismétlések (a hozzájuk tartozó BOOT-tal együtt) nem - ugyanaz az elv, mint
  // a TEST FAIL sorozatoknál.
  // Nem pillanatfelvétel: megvárjuk, elengedik-e. A részleteket lásd a
  // STUCK_BUTTON_CONFIRM_MS-nél - dióhéjban: az ébresztő gombnyomás alatt az
  // eszköz már bootol, tehát a gomb ilyenkor MINDIG lenyomva találtatik.
  int stuckButton = pressedButtonNow();
  if (stuckButton >= 0) {
    Serial.println("Gomb lenyomva indulaskor - megvarjuk, elengedik-e.");
    // PATTOGAS. A dontest EGYETLEN beolvasas hozza, nem kettő: ha a ciklus
    // feltételében olvasnánk, majd utána MÉGEGYSZER a minősítéshez, a két
    // olvasás közé beeshetne egy elengedéskori pattanás (a mechanikus gomb a
    // felengedéskor is ad néhány rövid visszaugrást), és a már elengedett
    // gombot beragadtnak minősítenénk. Így viszont a pattanás legfeljebb egy
    // további 10 ms-os kört jelent.
    bool elengedtek = false;
    const uint32_t vart = millis();
    while (millis() - vart < STUCK_BUTTON_CONFIRM_MS) {
      if (pressedButtonNow() < 0) {
        elengedtek = true;
        break;
      }
      feedWatchdog();
      delay(BUTTON_POLL_MS);
    }
    if (elengedtek) {
      stuckButton = -1;
      Serial.println("Elengedtek - ez ebreszto gombnyomas volt, indulunk tovabb.");
    }
  }
  const bool stuckRepeat =
    stuckButton >= 0 && stuckCycleAlreadyLogged((uint16_t)stuckButton);
  if (!stuckRepeat) {
    logEvent(EV_BOOT, (uint16_t)esp_reset_reason());
  }
  if (stuckButton == 0) {
    handleStuckButton("Reset button got stuck.", 0, !stuckRepeat);
  }
  if (stuckButton == 1) {
    handleStuckButton("Wifireset button got stuck.", 1, !stuckRepeat);
  }

  // A beragadt gomb ellenőrzése lefutott, jöhetnek a megszakítások. Ezek
  // teszik lehetővé, hogy egy blokkoló HTTP kérés (max. 33 mp) alatti
  // gombnyomás se vesszen el - a hosszmérés, tehát a debounce, az ISR-ben
  // történik. Lásd a btnResetLatched leírását.
  armButtonLatches();

  checkWatchdogResets();
  initHeapState();
  applyHeapCarry();

  Serial.println("Init LittleFS.");
  fsReady = initLittleFS();

  if (!fsReady) {
    // A konfigurációt tároló fájlrendszer nem elérhető. Ez NEM azonos azzal,
    // hogy nincs még konfiguráció - itt tényleg hiba van.
    if (deviceMode != MODE_FATAL) {
      logEvent(EV_FATAL, 1);
      enterFatal("A LittleFS nem csatolhato, a wifi konfiguracio nem toltheto be.");
    }
  } else {
    // Load values saved in LittleFS
    ConfigStatus st = CONFIG_OK;
    if (readConfigValue(LittleFS, ssidPath, ssid, sizeof(ssid)) == CONFIG_ERROR) st = CONFIG_ERROR;
    // A jelszó kódolva van, ezért nagyobb pufferbe olvassuk, majd helyben
    // dekódoljuk. A puffer csak itt él, a veremben - nem globális.
    {
      char passRaw[SECRET_ENC_MAX + 1];
      if (readConfigValue(LittleFS, passPath, passRaw, sizeof(passRaw)) == CONFIG_ERROR) {
        st = CONFIG_ERROR;
        pass[0] = '\0';
      } else {
        decodeSecretInPlace(passRaw);
        strlcpy(pass, passRaw, sizeof(pass));
      }
    }
    if (readConfigValue(LittleFS, ipPath, ipStr, sizeof(ipStr)) == CONFIG_ERROR) st = CONFIG_ERROR;
    if (readConfigValue(LittleFS, gatewayPath, gatewayStr, sizeof(gatewayStr)) == CONFIG_ERROR) st = CONFIG_ERROR;

    if (st == CONFIG_ERROR && deviceMode != MODE_FATAL) {
      // A fájl létezik, de nem olvasható: sérült fájlrendszer. Ilyenkor nem
      // indulunk AP módba sem, mert a mentés is elbukna - inkább jelzünk.
      logEvent(EV_FATAL, 2);
      enterFatal("A mentett wifi konfiguracio nem olvashato (serult fajlrendszer).");
    }
  }

  Serial.println(ssid);
  Serial.print((unsigned)strlen(pass));
  Serial.println(" chars password loaded");
  Serial.println(ipStr);
  Serial.println(gatewayStr);

  // Ne írjuk a hitelesítő adatokat minden WiFi.begin()-nél az NVS-be (flash kímélés)
  WiFi.persistent(false);

  // Végzetes hibánál nem próbálkozunk sem csatlakozással, sem AP portállal.
  if (deviceMode != MODE_FATAL) {
    if (initWiFi()) {
      Serial.println("WIFI OK!");
      logEvent(EV_WIFI_OK, (uint16_t)rtcRetryRounds);
      rtcRetryRounds = 0;
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, HIGH);  //led on

    } else if (ssid[0] == '\0') {
      // Nincs mentett hálózat: csak a beállító portál segíthet.
      logEvent(EV_AP_MODE, 1);
      startConfigPortal();

    } else {
      // Van mentett hálózat, csak most nem érhető el. NEM megyünk azonnal AP
      // módba: áramszünet után a router jóval lassabban indul, mint az ESP.
      // A handleFirstStart() kivárja a firstStartDelay-t (legfeljebb 10 perc -
      // 60 mp-enként megnézi, hogy visszajött-e a hálózat ÉS az internet, és
      // akkor korábban lép tovább), majd egységesen 3 próbát tesz 30 mp
      // szünetekkel.
      printUptime();
      Serial.println("Nem sikerult csatlakozni - first start varakozas kovetkezik.");
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, LOW);
    }
  }

}

void loop() {
  const uint32_t currentMillis = millis();

  // Az AP-módú beállító oldal kérésére halasztott újraindítás.
  //
  // A türelmi idő alatt (2 mp) ÚJABB mentés is érkezhet - mobilon a dupla
  // koppintás gyakori. Ilyenkor épp fájlírás folyik, és az újraindítás félbe
  // vágná: előbb megvárjuk, hogy az írás befejeződjön - ÉS mindjárt meg is
  // szerezzük a zárat, hogy a várakozás vége és az ESP.restart() közötti
  // néhány ezredmásodpercben már ne indulhasson újabb mentés.
  if (restartPending && (int32_t)(currentMillis - restartAt) >= 0) {
    lockConfigBeforeShutdown();
    restartPending = false;
    Serial.println("RESTART!");
    Serial.flush();
    ESP.restart();
  }

  // Hibátlanul lefutott egy óra: a watchdog számláló nullázható.
  //
  // MIÉRT ITT, a mód-elágazás ELŐTT? Mert a "hibátlan működés" nem üzemmód
  // kérdése. Korábban ez a monitor ág belsejében állt, így AP beállító módban
  // (és a first start várakozás alatt) sosem futott le: egy órákig nyitva
  // tartott portál mellett az eszköz régi, elavult watchdog-strike-okat
  // cipelt volna magával, és egy jóval későbbi, magában ártalmatlan glitch
  // vitte volna a hármas küszöbre - vagyis feleslegesen MODE_FATAL-ba.
  // (Mérve: WDT9.)
  //
  // A FELTETEL ALAKJA. Ez volt az EGYETLEN abszolut millis() osszehasonlitas a
  // programban (a masik 23 mind kulonbseg-alaku). Ket okbol lett belole is
  // kulonbseg:
  //
  // 1. KORBEFORDULAS. A millis() 49,7 naponta nullara fordul. Abszolut alakban
  //    a feltetel a fordulas utan egy oran at hamis lenne. (Ma nem okozna
  //    hibat, mert a szamlalo addigra ugyis nullazva van - de ez ervelessel
  //    igazolt biztonsag, nem a kifejezes alakjabol kovetkezo. A kulonbseg
  //    alak elojel nelkuli aritmetikaval magatol atvesszeli a fordulast.)
  //
  // 2. AZ INDULASI PONT KIMONDASA. Az abszolut alak azt a hallgatolagos
  //    feltevest hordozta, hogy a millis() MINDEN indulaskor nullarol kezd.
  //    Ez IGAZ - az ESP-IDF kimondja, hogy deep sleepbol ebredve az esp_timer
  //    (es igy az Arduino millis(), ami ebbol szarmazik) NULLAROL indul ujra;
  //    a sleep idejevel csak a LIGHT sleep utan lep elore, es csak a
  //    gettimeofday() az, ami a deep sleepet is atvinne. Igy viszont a
  //    feltetel maga mondja ki, mihez kepest mer: a setup() kezdetehez.
  //    (Merve: WDT14.)
  if (rtcWdtResets != 0 && currentMillis - timing.startMillis >= WDT_COUNTER_CLEAR_MS) {
    rtcWdtResets = 0;
    printUptime();
    Serial.println("1 ora hibatlan mukodes - a watchdog szamlalo nullazva.");
  }
  // Ugyanez a heap miatti ujrainditasok szamlalojara: egy ora hibatlan
  // mukodes utan a korabbi sorozat mar nem szamit. Kulon feltetel, hogy a
  // sorai kulon-kulon jelenjenek meg a soros porton.
  if (rtcHeapRestarts != 0 && currentMillis - timing.startMillis >= WDT_COUNTER_CLEAR_MS) {
    rtcHeapRestarts = 0;
    printUptime();
    Serial.println("1 ora hibatlan mukodes - a heap ujrainditas szamlalo nullazva.");
  }

  // A heap felugyelete MINDEN uzemmodban mer es kiir (a diagnosztika ott is
  // kell, ahol epp baj van), az ujrainditas viszont csak monitor modban
  // tortenhet - lasd a checkHeap() kizarasait.
  checkHeap(currentMillis);

  if (deviceMode == MODE_FATAL) {
    // Mindkét LED együtt, gyorsan villog. A program szándékosan nem fut
    // tovább: nem tesztel és nem kapcsolja a relét.
    if (currentMillis - timing.blinkLast >= FATAL_BLINK_MS) {
      timing.blinkLast = currentMillis;
      uiFlags.blinkOn = !uiFlags.blinkOn;
      digitalWrite(ledPin, uiFlags.blinkOn ? HIGH : LOW);
      digitalWrite(wifiledPin, uiFlags.blinkOn ? HIGH : LOW);
    }
    // A hiba magától nem múlik el: 5 perc jelzés után elalszunk, hogy ne
    // fogyasszunk és ne villogjunk feleslegesen napokig.
    if (currentMillis - timing.fatalStart >= FATAL_SLEEP_AFTER_MS) {
      fatalSleep();
    }
    // A gombok élnek, hogy újraindítani vagy resetelni lehessen az eszközt.
    resetbutton();
    wifiresetbutton();
    delay(BUTTON_POLL_MS);
    return;
  }

  if (deviceMode == MODE_CONFIG) {
    // A Wi-Fi LED villog: "beállító módban vagyok, várom a böngészőt". A
    // státusz LED közben végig világít - a kettő együtt egyértelműen más,
    // mint a végzetes hiba (mindkettő 5 Hz) vagy a router reset (csak a
    // státusz LED, 2 Hz).
    if (currentMillis - timing.blinkLast >= AP_BLINK_MS) {
      timing.blinkLast = currentMillis;
      uiFlags.blinkOn = !uiFlags.blinkOn;
      digitalWrite(wifiledPin, uiFlags.blinkOn ? HIGH : LOW);
    }
    // Konfig módban nincs internetteszt. A portál AP_TIMEOUT_MS tétlenségig
    // él; minden kérés és a folyamatban lévő mentés kitolja a határidőt.
    resetbutton();
    wifiresetbutton();
    // Nem alszunk el, ha épp mentés folyik, vagy ha a sikeres mentés utáni
    // újraindításra várunk.
    if (!savingConfig && !restartPending &&
        (int32_t)(currentMillis - apDeadline) >= 0) {
      apSleep();
    }
    delay(BUTTON_POLL_MS);
    return;
  }

  if (uiFlags.firstStart) {
    handleFirstStart(currentMillis);
    return;  // így biztosan nem fut le semmi más ebben a körben
  }

  resetbutton();
  wifiresetbutton();

  switch (currentState) {

    case TESTING_STATE: {
      if (WiFi.status() != WL_CONNECTED) {
        // SPAM-VEDELEM. Egy pislákoló kapcsolatnál (a jel a határon van, a
        // driver állapota másodpercenként többször vált) ez az ág körönként
        // újra lefutna, és a 32 elemű körpuffer kiszorítaná a kivizsgálandó
        // eseményeket. A SULYOSSAGROL OSZINTEN: védelem nélkül, REALIS
        // pislákolási ütemeknél (500 / 1000 / 2000 / 5000 ms) a mérés 4 / 2 /
        // 1 / 0 bejegyzést adott, tehát a puffert nem söpörte el - a
        // mechanizmus viszont valós, és a pislákolás gyorsulásával arányosan
        // romlik. Ugyanaz a szabály, mint a TEST FAIL sorozatoknál: csak a
        // sorozat ELSŐ tagja kerül a naplóba és a soros portra. Egy közbeeső
        // másik esemény után újra naplózunk. (Mérve: LOG4.)
        const bool ismetles = lastEventWas((uint8_t)EV_WIFI_LOST);
        if (!ismetles) {
          printUptime();
          logEvent(EV_WIFI_LOST, (uint16_t)WiFi.status());
          Serial.println("WiFi disconnected before test!");
        }
        digitalWrite(wifiledPin, LOW);

        // Egységes politika: 3 próba 30 mp szünetekkel.
        if (reconnectWifi()) {
          // Visszajött, a teszt a következő körben fut le.
          timing.stateStart = millis();
          break;
        }

        // Nem jött vissza: azonnal router újraindítás, nem várunk további
        // teszt ciklusokat. A FAILURE_STATE reset ágát így élesítjük - nincs
        // hálózat, amin bármelyik végpont elérhető lenne, nincs mit végigpróbálni.
        printUptime();
        Serial.println("WiFi nem jott vissza - router ujrainditas kovetkezik.");
        testState.cycleIndex = RESET_TRIGGER_CYCLE + 1;
        currentState = FAILURE_STATE;
        timing.stateStart = millis();
        break;
      }
      // A kapcsolat magától is helyreállhat (auto-reconnect), ilyenkor a LED
      // korábban hazudott volna.
      digitalWrite(wifiledPin, HIGH);
      if (rtcRetryRounds != 0) {
        rtcRetryRounds = 0;  // működik a hálózat: új 2 napos ablak
      }
      printUptime();
      Serial.println("Beginning Test.");
      // Csak az indexet írjuk ki ITT: az azt mondja meg, MELYIK végpont jön,
      // tehát a teszt ELŐTT van értelme. A hibaszámláló viszont a teszt
      // EREDMÉNYE - ezért az a "Test failed." sorra került. Korábban itt állt,
      // így a sorozat első tesztjénél mindig "0"-t írt ki.
      //
      // A KIÍRÁS 1-alapú (1..5), a belső cycleIndex marad 0-alapú: a 0..4
      // tartomány a végpontválasztó if-lánc és a RESET_TRIGGER_CYCLE
      // küszöb miatt kötött. Emberi olvasónak viszont nincs "0-dik teszt".
      Serial.print("Teszt ciklus index = ");
      Serial.println(testState.cycleIndex + 1);

      // Mind az ot teszt HTTP, mert az ICMP nem bizonyit sem nevfeloldast, sem
      // TCP-t: egy befagyott router-DNS mellett a ping tokeletesen megy, kozben
      // egyetlen eszkoz sem eri el az internetet. Nem veletlen, hogy egyetlen
      // nagy implementacio sem ICMP-vel validal (NetworkManager, Firefox,
      // Windows NCSI: mind HTTP). Ot kulonbozo uzemelteto, ket ellenorzesi mod.
      // Ures elvart valasz = 204-es ellenorzes, lasd testInternetHTTP().
      bool testResult;
      if (testState.cycleIndex == 1) {
        testResult = testInternetHTTP("http://cp.cloudflare.com/generate_204", "");
      } else if (testState.cycleIndex == 2) {
        testResult = testInternetHTTP("http://detectportal.firefox.com/success.txt", "success");
      } else if (testState.cycleIndex == 3) {
        testResult = testInternetHTTP("http://nmcheck.gnome.org/check_network_status.txt",
                                      "NetworkManager is online");
      } else if (testState.cycleIndex == 4) {
        testResult = testInternetHTTP("http://connectivitycheck.gstatic.com/generate_204", "");
      } else {
        testResult = testInternetHTTP("http://www.msftconnecttest.com/connecttest.txt", "Microsoft Connect Test");
      }

      if (testResult) {
        testState.cycleIndex = 0;
        testState.failedCount = 0;
        testState.resetEvents = 0;
        currentState = SUCCESS_STATE;
      } else {
        testState.failedCount++;
        // Csak a hibasorozat első tagját naplózzuk: a 12 mp-enként ismétlődő
        // bejegyzések különben percek alatt kiszorítanák a fontos eseményeket.
        if (testState.failedCount == 1) {
          // 1-alapú, hogy a /log oldal ugyanazt a számot mutassa, mint a
          // soros port "Teszt ciklus index" sora.
          logEvent(EV_TEST_FAIL, (uint16_t)(testState.cycleIndex + 1));
        }
        printUptime();
        // A már megnövelt számláló: ez a mostani bukással együtt hány
        // egymás utáni sikertelen teszt van. A nevező az a darabszám, ami
        // után a router újraindítása következik (mind az öt végpont bukott).
        Serial.print("Test failed. | Hibák száma = ");
        Serial.print(testState.failedCount);
        Serial.print(" / ");
        Serial.println(MAX_CYCLE_INDEX + 1);
        currentState = FAILURE_STATE;
      }
      // A tesztek percekig futhatnak, ezért friss időbélyeg kell.
      timing.stateStart = millis();
      break;
    }

    case FAILURE_STATE:
      // Mind az ot vegpont elbukott: a 4-es indexen a Google-t is probaltuk,
      // tehat a 0..4 mind lefutott es mind bukott. Mivel a ket szamlalo egyutt
      // lep (failedCount == cycleIndex + 1), ez pontosan 5 egymas utani bukas -
      // de a feltetel szandekosan a VEGPONT-lefedettseget mondja ki, nem az
      // idot: egyetlen uzemelteto kiesese soha ne latszodjon internetkimaradasnak.
      // Egy failedCount-alapu kuszob ugyanezt ma szam szerint eltalalna, de nem
      // ezt garantalna - ezert csak az index kot. A failedCount a naplozashoz es
      // a soros kimenethez kell (csak a hibasorozat elso tagja kerul a naplóba).
      if (testState.cycleIndex > RESET_TRIGGER_CYCLE) {

        if (!uiFlags.resetPrinted) {
          // Statikus IP mellett: ha a saját gateway-ünket sem érjük el, a hiba
          // helyi, és a router újraindítása nem javíthatja. Egy esélyt azért
          // adunk neki (hátha tényleg a router akadt meg) - a döntés a reset
          // UTÁNI ellenőrzésnél születik meg.
          if (gatewayUnreachable()) {
            printUptime();
            Serial.println("A sajat gateway sem elerheto - lehet, hogy rossz a statikus IP.");
            Serial.println("Kap a router egy esélyt: ujrainditas, aztan ujra ellenorizzuk.");
            logEvent(EV_GW_UNREACHABLE, 1);
          }
          printUptime();
          Serial.println("Beginning Reset in FAILURE_STATE.");
          while (!reset_device()) {
            resetbutton();
            wifiresetbutton();
            feedWatchdog();
            delay(BUTTON_POLL_MS);
          }
          printUptime();
          Serial.println("Reset is done in FAILURE_STATE.");
          Serial.println("RESET_DELAY start in FAILURE_STATE.");
          timing.stateStart = millis();
          uiFlags.resetPrinted = true;
          resetDelayProbeLast = millis();  // az első próba egy intervallum múlva
          break;                        // a RESET_DELAY a következő körökben telik
        }

        // A RESET_DELAY korai lezárása: ha a router hamarabb feláll, és az
        // internet is megvan, nincs mire várni.
        const bool earlyOnline = onlineProbeDue(resetDelayProbeLast, millis());

        if (earlyOnline || millis() - timing.stateStart >= RESET_DELAY) {
          printUptime();
          if (earlyOnline) {
            Serial.println("RESET_DELAY korai vege: halozat es internet OK.");
          } else {
            Serial.println("RESET_DELAY end in FAILURE_STATE.");
          }

          // Korai kilépéskor a kapcsolat MÁR él és mérve is van - a
          // disconnect(true) épp azt bontaná le, amit az imént igazoltunk.
          if (!earlyOnline) {
            Serial.println("Reconnect WIFI in FAILURE_STATE.");
            WiFi.disconnect(true);
            blockingDelay(100);

            // Egységesen 3 próba 30 mp szünetekkel. A reconnectWifi() minden
            // próbája teljes initWiFi(), ami újra alkalmazza a statikus IP/DNS
            // konfigot - a disconnect(true) ugyanis eldobja a netifet.
            if (!reconnectWifi()) {
              printUptime();
              Serial.println("A router reset utan sem jott vissza a WiFi.");
              digitalWrite(wifiledPin, LOW);
              wifiGiveUp();
              break;
            }

            printUptime();
            Serial.println("WIFI OK in FAILURE_STATE.");
          }
          digitalWrite(wifiledPin, HIGH);

          // A router megkapta az esélyét. Ha a gateway még mindig nem
          // válaszol, a statikus IP a rossz - a routert nincs értelme tovább
          // áramtalanítani. Beállító módba megyünk, hogy javítani lehessen.
          if (gatewayUnreachable()) {
            printUptime();
            Serial.println("A router ujrainditasa utan sem elerheto a gateway.");
            Serial.println("Valoszinuleg rossz a statikus IP - AP beallito mod.");
            logEvent(EV_GW_UNREACHABLE, 2);
            logEvent(EV_AP_MODE, 4);
            digitalWrite(wifiledPin, LOW);
            startConfigPortal();
            break;
          }

          testState.cycleIndex = 0;
          testState.failedCount = 0;
          uiFlags.resetPrinted = false;
          currentState = TESTING_STATE;
        }

      } else {
        if (currentMillis - timing.stateStart >= PROBE_DELAY) {
          if (testState.cycleIndex < MAX_CYCLE_INDEX) {
            testState.cycleIndex++;
          }
          currentState = TESTING_STATE;
        }
      }
      break;

    case SUCCESS_STATE:

      if (!uiFlags.successPrinted) {
        printUptime();
        Serial.println("Successful Test");
        Serial.println();
        Serial.println("SUCCESS_DELAY delay start.");
        uiFlags.successPrinted = true;
      }

      if (currentMillis - timing.stateStart >= SUCCESS_DELAY) {
        printUptime();
        Serial.println("SUCCESS_DELAY delay end.");
        uiFlags.successPrinted = false;
        timing.stateStart = currentMillis;
        currentState = TESTING_STATE;
      }
      break;
  }

  // A várakozó állapotok (SUCCESS 1 perc, FAILURE 12 mp) alatt a loop()-nak
  // nincs dolga. delay() nélkül 1. prioritáson pörögne 100% CPU-val; a
  // vTaskDelay viszont ténylegesen felfüggeszti a taskot. Minden időzítés
  // ezredmásodpercekben mér, tehát a 10 ms-os szemcsézettség nem számít.
  delay(BUTTON_POLL_MS);
}
