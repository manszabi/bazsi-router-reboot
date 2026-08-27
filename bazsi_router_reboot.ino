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

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char PARAM_SSID[]    = "ssid";
const char PARAM_PASS[]    = "pass";
const char PARAM_IP[]      = "ip";
const char PARAM_GATEWAY[] = "gateway";

// Tartalék űrlap arra az esetre, ha a data/ mappa nincs feltöltve a LittleFS-re.
// Flashben él, RAM-ot nem foglal.
const char FALLBACK_FORM[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>ESP Wi-Fi Manager</title></head><body>"
  "<h2>ESP Wi-Fi Manager</h2>"
  "<p><b>Figyelem:</b> a data/ mappa nincs feltoltve a LittleFS-re.</p>"
  "<form action=\"/\" method=\"POST\">"
  "SSID <input name=\"ssid\" maxlength=\"32\" required><br>"
  "Password <input name=\"pass\" type=\"password\" maxlength=\"63\"><br>"
  "IP <input name=\"ip\" maxlength=\"15\" placeholder=\"opcionalis\"><br>"
  "Gateway <input name=\"gateway\" maxlength=\"15\" placeholder=\"opcionalis\"><br>"
  "<input type=\"submit\" value=\"Submit\"></form>"
  "<p><a href=\"/log\">Diagnosztikai naplo</a></p></body></html>";

// AP password (WPA2: min. 8 karakter)
const char AP_PASSWORD[] = "bazsi1234";

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

// strapping pins 2, 8, 9
// Set LED GPIO, relay state
constexpr uint8_t ledPin = D4;
// wifi ok led
constexpr uint8_t wifiledPin = D3;
// Set RELAY pin, to router
constexpr uint8_t relayPin = D5;
// Set reset pin, esp wifireset pin
constexpr uint8_t wifiresetPin = D0;
// Set reset pin, esp reset/wakeup pin
constexpr uint8_t resetPin = D1;

// Timer variables
// interval to wait for Wi-Fi connection (milliseconds)
constexpr uint32_t interval = 20 * 1000;
constexpr uint32_t SUCCESS_DELAY = 1 * 60 * 1000;
constexpr uint32_t PROBE_DELAY = 12 * 1000;
constexpr uint32_t RESET_DELAY = 10 * 60 * 1000;
constexpr uint32_t RESET_PULSE = 90 * 1000;
constexpr uint32_t firstStartDelay = 10 * 60 * 1000;
constexpr uint8_t maxfailureEvents = 5;  // failure sleep
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
//  = 85,5 perc
// 2 nap = 2880 perc; 2880 / 85,5 = 33,7 -> 33 kör = 2821,5 perc = 47,0 óra,
// tehát még két napon belül. Ha ennyi idő alatt sem jön vissza a net, az már
// nem az eszköz dolga.
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
// Watchdog timeout. Nagyobb kell, mint a leghosszabb olyan blokkolás, amit NEM
// tudunk etetni: a http.GET() a connect (5 mp) + válasz (10 mp) timeouttal
// együtt ~15 mp-ig tarthat. 90 mp így hatszoros tartalékot ad.
constexpr uint32_t WDT_TIMEOUT_MS = 90 * 1000;
// Ennyi watchdog/panic miatti újraindulás után az eszközt instabilnak
// tekintjük, és ugyanúgy leállunk, mint a többi végzetes hibánál.
constexpr uint32_t MAX_WDT_RESETS = 3;
// Ennyi ideig tartó hibátlan működés után a számláló nullázódik.
constexpr uint32_t WDT_COUNTER_CLEAR_MS = 60 * 60 * 1000;
constexpr uint32_t RESTART_GRACE_MS = 2000;  // válasz kiküldése újraindítás előtt

constexpr uint64_t SLEEP_DURATION_US = 3600ULL * 1000000ULL;      // 1 óra
constexpr uint64_t STUCK_BUTTON_SLEEP_US = 60ULL * 1000000ULL;    // 60 másodperc

// Teszt paraméterek
constexpr uint8_t PING_ATTEMPTS = 4;
constexpr uint8_t PING_MIN_SUCCESS = 2;
constexpr uint32_t PING_GAP_MS = 1000;
constexpr size_t HTTP_MAX_PAYLOAD = 96;  // a várt válaszok < 32 bájt
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t HTTP_RESPONSE_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_READ_TIMEOUT_MS = 1500;
constexpr uint8_t MAX_CYCLE_INDEX = 10;
constexpr uint8_t RESET_TRIGGER_FAILURES = 3;
constexpr uint8_t RESET_TRIGGER_CYCLE = 3;

struct TestState {
  uint8_t cycleIndex = 0;   // volt: i
  uint8_t failedCount = 0;  // volt: failedTestsCount
  uint8_t resetEvents = 0;  // volt: Nreset_events
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
  bool successPrinted = false;     // volt: successfulTestPrinted
  bool resetPrinted = false;       // volt: beginResetPrinted
  bool firstStartPrinted = false;  // volt: firstStartPrinted
  bool firstStart = true;          // volt: firstStart
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

// Fut-e már a watchdog. A setup() blokkoló ciklusai (pl. az initWiFi() 20 mp-es
// várakozása) még az initWatchdog() ELŐTT futnak; ott a feedLoopWDT() hívás
// ESP_ERR_NOT_FOUND-ot kapna ("task not found"), amire a core log_e()-t hív -
// ez 20 mp alatt kétezer hibasort jelentene, ha be van kapcsolva a debug log.
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
  EV_TEST_FAIL = 4,     // param: teszt ciklus index
  EV_ROUTER_RESET = 5,  // param: hányadik reset esemény
  EV_AP_MODE = 6,       // param: ok (1=nincs SSID 2=auth hiba 3=2 nap letelt)
  EV_CONFIG_SAVED = 7,  // param: 0
  EV_SLEEP = 8,         // param: ok (1=retry 2=internet 3=AP timeout 4=fatal)
  EV_FATAL = 9,         // param: ok (1=FS mount 2=konfig olvasás 3=watchdog)
  EV_WDT_RESET = 10,    // param: hányadik watchdog reset
  EV_STUCK_BUTTON = 11  // param: 0 = reset gomb, 1 = wifireset gomb
};

// Pontosan 8 bájt. A kitöltő mező explicit, hogy a RTC memóriában tárolt
// elrendezés akkor se változzon, ha a fordító igazítási szabályai eltérnek.
struct EventEntry {
  uint32_t uptimeSec;
  uint16_t param;
  uint8_t code;
  uint8_t reserved;
};

constexpr uint8_t EVLOG_SIZE = 32;
constexpr uint32_t EVLOG_MAGIC = 0x42415A4CUL;  // "BAZL"
RTC_NOINIT_ATTR uint32_t rtcEvMagic;
RTC_NOINIT_ATTR uint32_t rtcEvNext;   // következő írási pozíció (monoton nő)
RTC_NOINIT_ATTR EventEntry rtcEvents[EVLOG_SIZE];

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
void blockingDelay(uint32_t duration);
void waitWithButtons(uint32_t duration);
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
void startConfigPortal();
void enterFatal(const char* reason);
void enterDeepSleep(uint64_t timerUs);
bool initWiFi();
bool reconnectWifi();
bool writeConfigValue(fs::FS& fs, const char* path, const char* message);

// Esemény rögzítése a körpufferbe. Nem allokál, nem blokkol.
void logEvent(EventCode code, uint16_t param) {
  if (rtcEvMagic != EVLOG_MAGIC) {
    rtcEvMagic = EVLOG_MAGIC;
    rtcEvNext = 0;
    memset(rtcEvents, 0, sizeof(rtcEvents));
  }
  EventEntry& e = rtcEvents[rtcEvNext % EVLOG_SIZE];
  e.uptimeSec = (uint32_t)(esp_timer_get_time() / 1000000);
  e.code = (uint8_t)code;
  e.param = param;
  e.reserved = 0;
  rtcEvNext++;
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
    default: return "?";
  }
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
    Serial.println("Valószínű ok: a kiválasztott partíciós séma nem tartalmaz");
    Serial.println("'spiffs' cimkéju partíciót (Arduino IDE: Tools > Partition Scheme).");
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
//   2. az ESP_TASK_WDT_PANIC alapértéke 'n', azaz timeoutkor csak kiír egy
//      figyelmeztetést a soros portra, nem indít újra.
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
    // rosszabb lenne a semminél: a 15 mp-ig tartó HTTP teszt alatt újraindítana.
    Serial.print("FIGYELEM: a watchdog NEM vedi a loop()-ot (feliratkozas ");
    Serial.print(watchdogEnabled ? "OK" : "SIKERTELEN");
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

// Szándékosan nem az enterDeepSleep()-et hívja: itt a Wi-Fi és a webszerver
// még el sem indult, és gombébresztést sem szabad armolni - a beragadt gomb
// azonnal újraébresztené az eszközt, azaz végtelen boot loop lenne.
void handleStuckButton(const char* message, uint16_t which) {
  Serial.println(message);
  Serial.print("Alvas ");
  Serial.print((unsigned long)(STUCK_BUTTON_SLEEP_US / 1000000ULL));
  Serial.println(" masodpercre, utana ujraprobalkozas.");
  Serial.flush();
  logEvent(EV_STUCK_BUTTON, which);

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
    const bool ipValid = localIP.fromString(ipStr);
    const bool gatewayValid = localGateway.fromString(gatewayStr);
    if (!ipValid) {
      Serial.println("❌ Invalid IP format!");
    }
    if (!gatewayValid) {
      Serial.println("❌ Invalid gateway format!");
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
    } else {
      Serial.println("✅ Manual IP config applied.");
    }
  } else {
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
  return true;
}

bool reset_device() {
  if (testState.resetStep == 0) {
    testState.resetEvents++;
    if (testState.resetEvents >= maxfailureEvents) {
      internetFailSleep();
    }
    logEvent(EV_ROUTER_RESET, (uint16_t)testState.resetEvents);
    Serial.println("Router resetting");
    Serial.print("Powering OFF the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, HIGH);
    Serial.println("Relay on.");
    digitalWrite(ledPin, LOW);
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
    digitalWrite(ledPin, HIGH);
    printUptime();
    testState.resetStep = 0;
    return true;
  }
  return false;
}

// FIGYELEM (hardver): deep sleep alatt az ESP32-C3 digitális lábai (GPIO6-21)
// nagyimpedanciás állapotba kerülnek, és csak az RTC lábak (GPIO0-5) tarthatók
// meg hold funkcióval. A relé a D5 = GPIO7-en van, tehát az alvás teljes ideje
// alatt LEBEG - szoftverből nem tartható. A relé vezérlőbemenetére külső
// lehúzó ellenállás kell (aktív-HIGH modulnál 10k GND felé), különben az
// alvás alatt véletlenül áramtalaníthatja a routert.
// Közös elalvás. timerUs = 0 esetén NINCS időzített ébresztés: az eszköz
// magától nem tér vissza, csak a reset gombra vagy áramtalanításra.
void enterDeepSleep(uint64_t timerUs) {
  digitalWrite(ledPin, LOW);  //led gnd, led off
  digitalWrite(relayPin, LOW);
  digitalWrite(wifiledPin, LOW);
  WiFi.disconnect(true);
  server.end();
  Serial.flush();
  Serial.end();

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
  waitWithButtons(RESET_DELAY);
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
  enterDeepSleep(SLEEP_DURATION_US);
}

// Nem sikerult csatlakozni a 3 probaval sem. Itt dol el, hogy tovabb varunk-e
// vagy beallito modba megyunk.
//
// A core meg tudja kulonboztetni a ket esetet (STA.cpp): WIFI_REASON_NO_AP_FOUND
// -> WL_NO_SSID_AVAIL (a halozat nem is latszik), WIFI_REASON_AUTH_FAIL ->
// WL_CONNECT_FAILED (rossz jelszo). Konzervativan dontunk: CSAK az explicit
// hitelesitesi hiba kuld AP modba. Minden mas esetben ujraprobalkozunk, mert
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

void resetbutton() {
  if (digitalRead(resetPin) != LOW) {
    timing.resetBtnDownSince = 0;  // felengedve: debounce újraindul
    return;
  }
  const uint32_t now = millis();
  if (timing.resetBtnDownSince == 0) {
    timing.resetBtnDownSince = now;
    return;
  }
  // Csak akkor fogadjuk el, ha végig lenyomva maradt (valódi debounce)
  if (now - timing.resetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    Serial.println("Reset button pressed.");
    Serial.println("RESTART ESP32C3 device.");
    Serial.flush();
    ESP.restart();
  }
}

void wifiresetbutton() {
  if (digitalRead(wifiresetPin) != LOW) {
    timing.wifiResetBtnDownSince = 0;
    return;
  }
  const uint32_t now = millis();
  if (timing.wifiResetBtnDownSince == 0) {
    timing.wifiResetBtnDownSince = now;
    return;
  }
  if (now - timing.wifiResetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    Serial.println("WIFIRESET button is pulling down!");
    Serial.println("RESET saved wifi data!");
    bool cleared = true;
    cleared &= clearConfigValue(LittleFS, gatewayPath);
    cleared &= clearConfigValue(LittleFS, ipPath);
    cleared &= clearConfigValue(LittleFS, passPath);
    cleared &= clearConfigValue(LittleFS, ssidPath);
    if (!cleared) {
      // Az újraindítás után a régi adatokkal jönne fel - legalább tudja a felhasználó.
      Serial.println("!!! A mentett wifi adatok törlése NEM sikerült !!!");
    }
    Serial.println("RESTART ESP32C3 device.");
    Serial.flush();
    ESP.restart();
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

// Korlátozott méretű, időzáras olvasás: nem allokál, és nem tud "elszállni"
// egy captive portal többszáz kilobájtos válaszán.
size_t readBounded(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  uint32_t lastByte = millis();
  while (n < maxLen && (millis() - lastByte) < timeoutMs) {
    // Csak akkor olvasunk, ha VAN mit: a read() a socket saját fogadási
    // timeoutját használja, ami nem a mienk. Ha vakon hívnánk, egyetlen
    // olvasás túlléphetné a timeoutMs határidőt - így viszont a határidő
    // valóban a miénk, és nem függ a core beállításaitól.
    if (stream.available() <= 0) {
      // A szerver lezárta és nincs több adat: nincs értelme a timeoutot kivárni
      if (!stream.connected()) {
        break;
      }
      resetbutton();
      wifiresetbutton();
      feedWatchdog();
      // delay() és nem yield(): a yield() csak azonos prioritású taskok között
      // ad át vezérlést, tehát üresen pörgetné a CPU-t a válaszra várva.
      delay(BUTTON_POLL_MS);
      continue;
    }
    const int c = stream.read();
    if (c < 0) {
      break;  // available() adatot ígért, mégsem jött: a kapcsolat elszállt
    }
    buf[n++] = (char)c;
    lastByte = millis();
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

  const int httpCode = http.GET();
  bool result = false;

  if (httpCode == HTTP_CODE_OK) {
    const int len = http.getSize();
    if (len > (int)HTTP_MAX_PAYLOAD) {
      // Ekkora választ nem a várt endpoint küld (pl. captive portal)
      Serial.print("Unexpected payload size: ");
      Serial.println(len);
    } else {
      const size_t want = (len > 0) ? (size_t)len : HTTP_MAX_PAYLOAD;
      char payload[HTTP_MAX_PAYLOAD + 1];
      // Ugyanaz a stream, amit a http.begin() kapott — nem függünk a
      // getStream() core-verziónként eltérő visszatérési típusától.
      const size_t n = readBounded(client, payload, want, HTTP_READ_TIMEOUT_MS);
      payload[n] = '\0';
      trimInPlace(payload);  // a záró CR/LF ne buktassa el az egyezést
      Serial.println(payload);
      result = (strcmp(payload, expectedResponse) == 0);
      Serial.println(result ? "Igaz érték!" : "Hamis érték!");
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
    IPAddress dest = target;  // a Ping.ping() érték szerint vár paramétert
    const bool pingOK = Ping.ping(dest, 1);  // 1 próbálkozás pingenként
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

  return successCount >= PING_MIN_SUCCESS;
}

void handleFirstStart(uint32_t currentMillis) {
  if (WiFi.status() != WL_CONNECTED) {
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
  }
  // Innentől a firstStart lezárult: a timing.startMillis-t senki nem olvassa
  // többé, ezért nincs értelme frissíteni.
  uiFlags.firstStart = false;
}

void startConfigPortal() {
  if (deviceMode == MODE_CONFIG) {
    return;  // már fut
  }
  deviceMode = MODE_CONFIG;
  touchApDeadline();
  digitalWrite(wifiledPin, LOW);  //led off

  Serial.println("Setting AP (Access Point)");
  char apName[32];
  snprintf(apName, sizeof(apName), "ESP-%s", ESP.getChipModel());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName, AP_PASSWORD);
  Serial.print("AP SSID: ");
  Serial.println(apName);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Web Server Root URL. Ha a data/ mappa nem került fel a LittleFS-re, a
  // beginResponse(FS&,...) NULL-t ad és a kliens 501-et kapna - ilyenkor az
  // eszköz konfigurálhatatlan lenne, ezért beépített tartalék űrlapot adunk.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    if (LittleFS.exists("/wifimanager.html")) {
      request->send(LittleFS, "/wifimanager.html", "text/html");
    } else {
      request->send(200, "text/html", FALLBACK_FORM);
    }
  });

  // Csak a weboldal statikus elemeit szolgáljuk ki. A serveStatic("/") a teljes
  // LittleFS-t kiadta volna, azaz a /pass.txt-ben tárolt Wi-Fi jelszót is.
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    request->send(LittleFS, "/style.css", "text/css");
  });
  server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    request->send(LittleFS, "/favicon.png", "image/png");
  });
  // Diagnosztikai napló. Ez az egyetlen mód, hogy soros kábel nélkül megtudd,
  // mi történt az eszközzel - és épp AP módban vagy, amikor baj van.
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    // A stream puffere igény szerint nő (resizeAdd), de akkor soronként
    // újraallokálna. Egy bőséges kezdőmérettel ez egyetlen foglalás lesz:
    // fejléc + állapot + 32 sor x ~70 bájt + lábléc alatta marad.
    AsyncResponseStream* r = request->beginResponseStream("text/html", 4096);
    r->print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
               "<title>Naplo</title></head><body><h2>Diagnosztikai naplo</h2>"));

    r->printf("<p>Utolso indulas oka: %d<br>", (int)esp_reset_reason());
    r->printf("Watchdog ujraindulasok: %u / %u<br>",
              (unsigned)rtcWdtResets, (unsigned)MAX_WDT_RESETS);
    r->printf("Ujraprobalkozasi korok: %u / %u<br>",
              (unsigned)rtcRetryRounds, (unsigned)MAX_RETRY_ROUNDS);
    r->printf("Uptime: %u mp</p>", (unsigned)(esp_timer_get_time() / 1000000));

    if (rtcEvMagic != EVLOG_MAGIC || rtcEvNext == 0) {
      r->print(F("<p>Nincs rogzitett esemeny.</p>"));
    } else {
      r->print(F("<table border=1 cellpadding=4><tr><th>Uptime</th>"
                 "<th>Esemeny</th><th>Param</th></tr>"));
      // A legregebbi meg meglevo bejegyzestol indulunk
      const uint32_t total = rtcEvNext;
      const uint32_t shown = total < EVLOG_SIZE ? total : EVLOG_SIZE;
      for (uint32_t i = total - shown; i < total; i++) {
        const EventEntry& e = rtcEvents[i % EVLOG_SIZE];
        r->printf("<tr><td>%u:%02u:%02u</td><td>%s</td><td>%u</td></tr>",
                  (unsigned)(e.uptimeSec / 3600), (unsigned)((e.uptimeSec % 3600) / 60),
                  (unsigned)(e.uptimeSec % 60), eventName(e.code), (unsigned)e.param);
      }
      r->print(F("</table>"));
    }
    r->print(F("<p><i>Az uptime minden indulaskor nullarol indul, ezert a "
               "BOOT sorok jelzik az ujraindulasokat. A naplo az "
               "aramtalanitast nem eli tul.</i></p>"
               "<p><a href=\"/\">Vissza a beallitasokhoz</a></p></body></html>"));
    request->send(r);
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    if (!fsReady) {
      // Nincs értelme menteni: a fájlrendszer nem áll rendelkezésre.
      request->send(500, "text/plain",
                    "LittleFS nem elerheto, a beallitasok nem menthetok. "
                    "Ellenorizd a particios semat (Tools > Partition Scheme).");
      return;
    }

    // Amíg ez igaz, a loop() semmiképp nem altatja el az eszközt: fájlírás
    // közbeni deep sleep félig kiírt konfigurációt hagyna hátra.
    savingConfig = true;
    bool saveOk = true;
    bool ssidProvided = false;
    // Az első hiba oka, hogy a felhasználó konkrét visszajelzést kapjon.
    const char* failReason = nullptr;
    const int params = request->params();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter* p = request->getParam(i);
      if (!p->isPost()) {
        continue;
      }
      const String& name = p->name();
      const String& val = p->value();  // referencia: nincs felesleges másolat

      if (name == PARAM_SSID) {
        if (val.length() > 0 && val.length() <= SSID_MAX_LEN) {
          ssidProvided = true;
          strlcpy(ssid, val.c_str(), sizeof(ssid));
          Serial.print("SSID set to: ");
          Serial.println(ssid);
          saveOk &= writeConfigValue(LittleFS, ssidPath, ssid);
        } else {
          Serial.println("Invalid SSID length!");
          saveOk = false;
          failReason = "Ervenytelen SSID hossz (1-32 karakter).";
        }
      } else if (name == PARAM_PASS) {
        if (val.length() <= PASS_MAX_LEN) {
          strlcpy(pass, val.c_str(), sizeof(pass));
          Serial.print("Password set to: ");
          Serial.print(val.length());
          Serial.println(" chars");
          saveOk &= writeConfigValue(LittleFS, passPath, pass);
        } else {
          Serial.println("Password too long!");
          saveOk = false;
          if (failReason == nullptr) failReason = "A jelszo tul hosszu (max 63 karakter).";
        }
      } else if (name == PARAM_IP) {
        IPAddress testIP;
        if (val.length() == 0) {
          ipStr[0] = '\0';
          Serial.println("IP empty, using DHCP.");
          saveOk &= writeConfigValue(LittleFS, ipPath, "");
        } else if (val.length() <= IPSTR_MAX_LEN && testIP.fromString(val.c_str())) {
          strlcpy(ipStr, val.c_str(), sizeof(ipStr));
          Serial.print("IP Address set to: ");
          Serial.println(ipStr);
          saveOk &= writeConfigValue(LittleFS, ipPath, ipStr);
        } else {
          Serial.println("Invalid IP format!");
          saveOk = false;
          if (failReason == nullptr) failReason = "Ervenytelen IP cim formatum.";
        }
      } else if (name == PARAM_GATEWAY) {
        IPAddress testIP;
        if (val.length() == 0) {
          gatewayStr[0] = '\0';
          Serial.println("Gateway empty, using DHCP.");
          saveOk &= writeConfigValue(LittleFS, gatewayPath, "");
        } else if (val.length() <= IPSTR_MAX_LEN && testIP.fromString(val.c_str())) {
          strlcpy(gatewayStr, val.c_str(), sizeof(gatewayStr));
          Serial.print("Gateway set to: ");
          Serial.println(gatewayStr);
          saveOk &= writeConfigValue(LittleFS, gatewayPath, gatewayStr);
        } else {
          Serial.println("Invalid gateway format!");
          saveOk = false;
          if (failReason == nullptr) failReason = "Ervenytelen gateway formatum.";
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
    if ((ipStr[0] != '\0') != (gatewayStr[0] != '\0')) {
      saveOk = false;
      if (failReason == nullptr) {
        failReason = "Statikus IP-hez az IP cimet ES a gateway-t is meg kell adni "
                     "(DHCP-hez hagyd mindkettot uresen).";
      }
    }

    savingConfig = false;
    touchApDeadline();

    if (!saveOk) {
      // Ne hazudjunk sikert és főleg ne indítsunk újra: az újraindítás
      // eldobná a beírt adatokat, a felhasználó pedig ugyanitt kötne ki.
      Serial.println("A beallitasok mentese SIKERTELEN.");
      // A leghosszabb indoklás (statikus IP + gateway) a rögzített szöveggel
      // együtt 176 bájt; a snprintf() csonkolna, ha ennél kisebb lenne.
      char err[208];
      snprintf(err, sizeof(err),
               "A beallitasok mentese nem sikerult: %s "
               "Az eszkoz NEM indul ujra, probald meg ismet.",
               failReason ? failReason : "LittleFS irasi hiba.");
      request->send(500, "text/plain", err);
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

  pinMode(wifiresetPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(wifiledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(wifiledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(ledPin, HIGH);  //bekapcsolja a ledet, +5volt 150 ohm

  Serial.begin(115200);
  const uint32_t serialTimeout = millis();
  while (!Serial && millis() - serialTimeout < 3000) { delay(BUTTON_POLL_MS); }
  blockingDelay(500);  // USB CDC beállása, hogy az induló logok ne vesszenek el

  printUptime();

  logEvent(EV_BOOT, (uint16_t)esp_reset_reason());

  // Mindkét gombot ellenőrizzük: ha bármelyik beragadt, nem indulunk el.
  if (digitalRead(resetPin) == LOW) {
    handleStuckButton("Reset button got stuck.", 0);
  }
  if (digitalRead(wifiresetPin) == LOW) {
    handleStuckButton("Wifireset button got stuck.", 1);
  }

  checkWatchdogResets();

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
    if (readConfigValue(LittleFS, passPath, pass, sizeof(pass)) == CONFIG_ERROR) st = CONFIG_ERROR;
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
      // A handleFirstStart() kivárja a firstStartDelay-t (10 perc), majd
      // egységesen 3 próbát tesz 30 mp szünetekkel.
      printUptime();
      Serial.println("Nem sikerult csatlakozni - first start varakozas kovetkezik.");
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, LOW);
    }
  }

  // Utolsó lépés: innentől figyeli a watchdog a loop()-ot. A setup() saját
  // blokkolásai (soros port, initWiFi) így nem tudnak téves újraindítást okozni.
  initWatchdog();
}

void loop() {
  const uint32_t currentMillis = millis();

  // Az AP-módú beállító oldal kérésére halasztott újraindítás
  if (restartPending && (int32_t)(currentMillis - restartAt) >= 0) {
    restartPending = false;
    Serial.println("RESTART!");
    Serial.flush();
    ESP.restart();
  }

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

  // Hibátlanul lefutott egy óra: a watchdog számláló nullázható.
  if (rtcWdtResets != 0 && currentMillis >= WDT_COUNTER_CLEAR_MS) {
    rtcWdtResets = 0;
    printUptime();
    Serial.println("1 ora hibatlan mukodes - a watchdog szamlalo nullazva.");
  }

  switch (currentState) {

    case TESTING_STATE: {
      if (WiFi.status() != WL_CONNECTED) {
        printUptime();
        logEvent(EV_WIFI_LOST, (uint16_t)WiFi.status());
        Serial.println("WiFi disconnected before test!");
        digitalWrite(wifiledPin, LOW);

        // Egységes politika: 3 próba 30 mp szünetekkel.
        if (reconnectWifi()) {
          // Visszajött, a teszt a következő körben fut le.
          timing.stateStart = millis();
          break;
        }

        // Nem jött vissza: azonnal router újraindítás, nem várunk további
        // teszt ciklusokat. A FAILURE_STATE reset ágát így élesítjük.
        printUptime();
        Serial.println("WiFi nem jott vissza - router ujrainditas kovetkezik.");
        testState.cycleIndex = RESET_TRIGGER_CYCLE + 1;
        testState.failedCount = RESET_TRIGGER_FAILURES;
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
      Serial.print("Teszt ciklus index = ");
      Serial.print(testState.cycleIndex);
      Serial.print(" | Hibák száma = ");
      Serial.println(testState.failedCount);

      bool testResult;
      if (testState.cycleIndex == 1) {
        testResult = testInternetPing(IPAddress(1, 1, 1, 1), "Cloudflare");
      } else if (testState.cycleIndex == 3) {
        testResult = testInternetPing(IPAddress(8, 8, 8, 8), "Google");
      } else if (testState.cycleIndex == 2 || testState.cycleIndex == 4) {
        testResult = testInternetHTTP("http://www.msftncsi.com/ncsi.txt", "Microsoft NCSI");
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
          logEvent(EV_TEST_FAIL, (uint16_t)testState.cycleIndex);
        }
        printUptime();
        Serial.println("Test failed.");
        currentState = FAILURE_STATE;
      }
      // A tesztek percekig futhatnak, ezért friss időbélyeg kell.
      timing.stateStart = millis();
      break;
    }

    case FAILURE_STATE:
      if (testState.cycleIndex > RESET_TRIGGER_CYCLE && testState.failedCount >= RESET_TRIGGER_FAILURES) {

        if (!uiFlags.resetPrinted) {
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
          uiFlags.resetPrinted = true;  // Set the flag after printing
          break;                        // a RESET_DELAY a következő körökben telik
        }

        if (millis() - timing.stateStart >= RESET_DELAY) {
          printUptime();
          Serial.println("RESET_DELAY end in FAILURE_STATE.");
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
          digitalWrite(wifiledPin, HIGH);

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
        uiFlags.successPrinted = true;  // Set the flag to true after printing
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
