#include <Arduino.h>
#include <set>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPping.h>
#include <HTTPClient.h>
#include <esp_system.h>

#include <ESPAsyncWebServer.h>
#include <driver/gpio.h>
extern std::map<std::string, ArRequestHandlerFunction> g_handlers;
#include <cassert>
#include <unistd.h>
#include <sys/wait.h>
#ifdef COVERAGE_BUILD
extern "C" void __gcov_dump(void);
#endif

// --- a sketch globális állapota ---
void setup(); void loop();
extern char ssid[]; extern char pass[]; extern char ipStr[]; extern char gatewayStr[];
enum State : uint8_t;
enum DeviceMode : uint8_t;
extern DeviceMode deviceMode;
extern State currentState;
bool initWiFi(); bool reconnectWifi(); bool reset_device();
void trimInPlace(char*);
bool testInternetHTTP(const char* url, const char* expected);
void initWatchdog();
bool testInternetPing(const IPAddress& target, const char* name);
enum ConfigStatus : uint8_t;
ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize);
bool writeConfigValue(fs::FS& fs, const char* path, const char* msg);
bool clearConfigValue(fs::FS& fs, const char* path);
bool initLittleFS();
bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len);
extern bool fsReady;
extern uint32_t rtcWdtMagic;
extern uint32_t rtcWdtResets;
extern volatile bool savingConfig;
extern volatile uint32_t apDeadline;
extern volatile bool restartPending;
extern volatile uint32_t restartAt;
extern uint32_t rtcRetryRounds;
extern uint32_t rtcEvMagic;
extern uint32_t rtcEvNext;
enum EventCode : uint8_t;
struct EventEntry { uint32_t uptimeSec; uint16_t param; uint8_t code; uint8_t pad; };
extern EventEntry rtcEvents[];
// A sketch allapot-structjai. Sima globalisok, tehat egy valodi ujraindulas
// (a deep sleepbol ebredes is) nullazza oket - a coldBoot()-nak ugyanezt kell
// tennie. FIGYELEM: az alapertelmezett tagertekeknek egyeznie KELL a sketchbeli
// definicioval, kulonben a coldBoot() mas allapotbol indul, mint a valodi boot.
// Kulonosen a firstStart = true: enelkul a firstStart fazis kimaradna.
struct TestState { uint8_t cycleIndex = 0; uint8_t failedCount = 0;
                   uint8_t resetEvents = 0; uint8_t resetStep = 0; };
struct TimingState { uint32_t stateStart = 0; uint32_t resetPulseStart = 0;
                     uint32_t startMillis = 0; uint32_t resetBtnDownSince = 0;
                     uint32_t wifiResetBtnDownSince = 0; uint32_t blinkLast = 0;
                     uint32_t fatalStart = 0; };
struct UIFlags { bool successPrinted = false; bool resetPrinted = false;
                 bool firstStartPrinted = false; bool firstStart = true;
                 bool blinkOn = false; };
extern TestState testState;
extern TimingState timing;
extern UIFlags uiFlags;
extern bool staticConfigActive;
extern bool watchdogEnabled;
extern uint32_t resetDelayProbeLast;
extern uint32_t firstStartProbeLast;
bool onlineProbeDue(uint32_t& lastProbe, uint32_t now);
void logEvent(EventCode code, uint16_t param);
constexpr uint8_t EV_TEST_FAIL_C = 4;
constexpr uint8_t EV_FATAL_C = 9;
void resetbutton();
void wifiresetbutton();
void waitWithButtons(uint32_t);
void touchApDeadline();
void printUptime();
void decodeSecretInPlace(char* buf);
bool encodeSecret(const char* plain, char* out, size_t outSize);
void startConfigPortal();

// A sketch pinjei (variants/XIAO_ESP32C3/pins_arduino.h szerint). NEVESITVE:
// egy hardveres atkotes igy egyetlen sor a tesztekben is - korabban a relé
// GPIO10 (D5) volt, es a szama 37 helyen szerepelt szo szerint.
static const int PIN_RELAY   = 10;  // D10 - rele (kulso 22k lehuzoval)
static const int PIN_LED     = 6;   // D4  - statusz LED
static const int PIN_WIFILED = 5;   // D3  - Wi-Fi LED
#define RELAY_HIGH "pin10=HIGH"
#define RELAY_LOW  "pin10=LOW"
#define LED_HIGH   "pin6=HIGH"
#define LED_LOW    "pin6=LOW"

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if(!(cond)) { printf("  \033[31mFAIL\033[0m %s\n", msg); failures++; } \
                              else printf("  ok   %s\n", msg); } while(0)

// A sketch uiFlags struktúráját nem exportáljuk; a monitorozás kezdetét abból
// látjuk, hogy a WiFi csatlakozott és már nem a first start ágban vagyunk.
// Igaz, ha a soros kimeneten mar megjelent a keresett szoveg
static bool serialHas(const char* frag) {
  for (auto& l : g_serialLog) if (l.find(frag) != std::string::npos) return true;
  return false;
}

// Ugyanaz, de a SOROS kimeneten (a Serial.printf-fel irt sorok is itt vannak)
static int serialIndex(const char* frag) {
  for (size_t i = 0; i < g_serialLog.size(); i++)
    if (g_serialLog[i].find(frag) != std::string::npos) return (int)i;
  return -1;
}

// A jelszo kodolva kerul a fajlba. A szerzodes nem az, hogy MI van a fajlban,
// hanem hogy amit mentettunk, azt kapjuk vissza - es hogy a nyilt szoveg NINCS
// benne. Ez a segedfuggveny a fajl dekodolt tartalmat adja.
static std::string storedPass() {
  char buf[256];
  strlcpy(buf, g_fs["/pass.txt"].c_str(), sizeof(buf));
  decodeSecretInPlace(buf);
  return std::string(buf);
}

static int logIndex(const char* frag) {
  for (size_t i = 0; i < g_log.size(); i++) if (g_log[i].find(frag) != std::string::npos) return (int)i;
  return -1;
}

// Teljes újraindítás szimulálása.
// deepSleepWake = false: valódi bekapcsolás. A reset ok ESP_RST_POWERON, és az
//   RTC_DATA_ATTR rtcRetryRounds nullázódik, ahogy áramtalanításkor is.
// deepSleepWake = true: deep sleep utáni ébredés. A reset ok ESP_RST_DEEPSLEEP,
//   és az RTC memória megmarad - enélkül a több körön át tartó viselkedés
//   (MAX_RETRY_ROUNDS) egyáltalán nem lenne tesztelhető.
static void coldBoot(bool willConnect, const char* s, const char* p,
                     const char* ip, const char* gw, uint32_t latency = 500,
                     bool deepSleepWake = false) {
  g_millis = 1; g_log.clear(); g_serialLog.clear(); g_pinState.clear(); g_pinRead.clear();
  g_fs.clear(); g_fsMountOk = true; g_wakeupUs = 0;
  g_fsWritable = true; g_fsCapacity = 0; g_fsRemoveOk = true; g_fsReadable = true;
  g_resetReason = deepSleepWake ? ESP_RST_DEEPSLEEP : ESP_RST_POWERON;
  if (!deepSleepWake) {
    rtcRetryRounds = 0;
  }
  // Egy valodi hidegindulas ezeket is nullazza (sima globalisok). Enelkul egy
  // korabbi mentes utan beallitott halasztott ujrainditas atszivarogna a
  // "reboot" utanra, es kesobb, a scenario kozepen inditana ujra az eszkozt.
  restartPending = false; restartAt = 0; savingConfig = false;
  // A sketch sima globalisai: egy valodi ujraindulas (a deep sleepbol ebredes
  // is) ezeket nullazza, mert nem RTC memoriaban vannak. Enelkul egy tobb
  // bootot vegigjatszo forgatokonyvben a resetEvents atszivarogna, es a
  // masodik kortol mar az internetFailSleep() futna a wifiGiveUp() helyett.
  testState = TestState();
  timing = TimingState();
  uiFlags = UIFlags();
  // A korai kilepes probainak orai is sima globalisok: egy valodi ujraindulas
  // nullazza oket. A setup() ugyan mindkettot beallitja hasznalat elott, de a
  // coldBoot() szerzodese az, hogy MINDENT ugy hagy, mint egy hidegindulas.
  resetDelayProbeLast = 0;
  firstStartProbeLast = 0;
  staticConfigActive = false;
  watchdogEnabled = false;
  g_gpioWakeResult = 0;
  g_gpioWakeMask = 0; g_gpioWakeMode = -1;
  // A GPIO hold a valosagban tuleli az ebredest (reset), csak az
  // aramtalanitas torli - a modell is igy tesz.
  g_gpioHoldFails = false;
  if (!deepSleepWake) { g_heldPins.clear(); g_deepSleepHoldEnabled = false; }
  g_httpCode = 200; g_httpSize = -2; g_httpBeginOk = true; g_httpBody = "Microsoft NCSI";
  g_httpChunked = false; g_httpChunkSize = 0; g_httpRawOverride.clear();
  g_httpUrls.clear();
  wifiSim.reset(); pingSim = PingSim();
  wifiSim.willConnect = willConnect; wifiSim.latencyMs = latency;
  if (s && *s)  g_fs["/ssid.txt"] = s;
  if (p && *p)  g_fs["/pass.txt"] = p;
  if (ip && *ip) g_fs["/ip.txt"] = ip;
  if (gw && *gw) g_fs["/gateway.txt"] = gw;
  ssid[0] = pass[0] = ipStr[0] = gatewayStr[0] = '\0';
  deviceMode = (DeviceMode)0; currentState = (State)0;
}

static void sc0() {
  coldBoot(false, "", "", "", "");
  try { setup(); CHECK(true, "setup() lefutott deep sleep nélkül"); }
  catch (DeepSleepSignal&) { CHECK(false, "setup() NEM aludhat el SSID hiányában"); }
  CHECK(deviceMode == (DeviceMode)1, "MODE_CONFIG lett");
  CHECK(wifiSim.softApCount == 1, "softAP() elindult");
  CHECK(wifiSim.beginCount == 0, "WiFi.begin() nem futott (nincs SSID)");
  {
    // 15 percnyi loop konfig módban -> nem szabad elaludnia (a korábbi hiba)
    bool slept = false;
    const uint32_t t0 = g_millis;
    try { for (int i = 0; i < 200000 && g_millis - t0 < 4u*60*1000; i++) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    catch (RestartSignal&) {}
    // A régi hiba: 3 perc után elaludt a portál. Most 5 percig biztosan él.
    CHECK(!slept, "4 percig biztosan él a portál (regresszió)");
    CHECK(deviceMode == (DeviceMode)1, "konfig módban maradt");
  }

}

static void sc1() {
  coldBoot(true, "TestNet", "secret123", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "MODE_MONITOR lett");
  CHECK(wifiSim.configCount == 0, "üres IP/GW esetén nincs WiFi.config() -> DHCP");
  CHECK(wifiSim.beginCount == 1, "pontosan egy WiFi.begin()");
  CHECK(g_pinState[5] == HIGH, "wifi LED (GPIO5) bekapcsolt");
  CHECK(logIndex("WiFi.mode(STA)") < logIndex("WiFi.begin"), "mode(STA) a begin() ELŐTT");

}

static void sc2() {
  coldBoot(true, "TestNet", "secret123", "192.168.1.200", "192.168.1.1");
  setup();
  CHECK(wifiSim.configCount == 1, "WiFi.config() lefutott");
  CHECK(wifiSim.cfgDns1.str() == "192.168.1.1", "DNS1 = gateway (nem 0.0.0.0!)");
  CHECK(wifiSim.cfgDns2.str() == "1.1.1.1", "DNS2 = 1.1.1.1 tartalék");
  CHECK(logIndex("WiFi.mode(STA)") < logIndex("WiFi.config"), "mode(STA) a config() ELŐTT");
  CHECK(logIndex("WiFi.config") < logIndex("WiFi.begin"), "config() a begin() ELŐTT");

}

static void sc3() {
  coldBoot(true, "TestNet", "pw", "nem-ip", "192.168.1.1");
  setup();
  CHECK(wifiSim.configCount == 0, "érvénytelen IP -> nincs config(), DHCP");
  CHECK(deviceMode == (DeviceMode)0, "attól még csatlakozik (monitor mód)");

}

static void sc4() {
  coldBoot(false, "TestNet", "pw", "", "");
  { uint32_t t0 = g_millis; setup(); uint32_t dt = g_millis - t0;
    CHECK(dt >= 20000 && dt < 26000, "initWiFi ~20s után adta fel");
    // Mentett SSID mellett NEM megyünk AP módba, hanem újrapróbálkozunk
    // (a részleteket a WF1-WF5 fedi).
    CHECK(deviceMode == (DeviceMode)0, "monitor módban marad és újrapróbál"); }

}

static void sc5() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { int before = wifiSim.beginCount;
    bool r = initWiFi();
    CHECK(r, "initWiFi() true-t adott");
    CHECK(wifiSim.beginCount == before, "NEM hívott újabb WiFi.begin()-t"); }

}

static void sc6() {
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "192.168.1.1");
  setup();
  { g_log.clear(); wifiSim.configCount = 0;
    // A FAILURE_STATE ág pontosan ezt teszi: disconnect(true) majd újracsatlakozás
    WiFi.disconnect(true);
    CHECK(!wifiSim.staticApplied, "disconnect(true) eldobta a netif statikus configját");
    bool r = initWiFi();
    CHECK(r, "újracsatlakozás sikerült");
    CHECK(wifiSim.configCount == 1, "a statikus IP/DNS ÚJRA alkalmazva lett");
    CHECK(wifiSim.staticApplied, "nem esett vissza DHCP-re");
    CHECK(wifiSim.cfgDns1.str() == "192.168.1.1", "a DNS is visszaállt"); }

}

static void sc7() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { wifiSim.willConnect = false; wifiSim.begun = false;
    g_log.clear(); wifiSim.beginCount = 0;
    uint32_t t0 = g_millis; bool slept = false; bool ret = true;
    try { ret = reconnectWifi(); } catch (DeepSleepSignal&) { slept = true; }
    CHECK(!slept, "NEM alszik el - a folytatásról a hívó dönt");
    CHECK(!ret, "false-t ad vissza");
    CHECK(wifiSim.beginCount == 3, "pontosan 3 csatlakozási kísérlet");
    uint32_t dt = g_millis - t0;
    CHECK(dt >= 120000 && dt < 150000, "3x20s timeout + 2x30s szünet (~120s)"); }

}

static void sc8() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { wifiSim.willConnect = false; wifiSim.begun = false; wifiSim.beginCount = 0;
    // a 2. kísérlet közben jön vissza a hálózat
    bool ok = false;
    // 25s után engedjük csatlakozni
    uint32_t releaseAt = g_millis + 25000;
    // egyszerűbb: kézzel léptetjük
    (void)releaseAt;
    wifiSim.willConnect = false;
    // szimuláljuk: az 1. initWiFi elbukik, majd engedélyezzük
    try {
      // 1. kísérlet
      ok = initWiFi();
      CHECK(!ok, "1. kísérlet elbukott");
      wifiSim.willConnect = true;
      ok = initWiFi();
      CHECK(ok, "2. kísérlet sikerült, miután visszajött a hálózat");
    } catch (DeepSleepSignal&) { CHECK(false, "nem lett volna szabad elaludnia"); } }

  // ============ SLEEP ============
}

static void sc9() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[3] = LOW;  // D1 = GPIO3 reset gomb lenyomva
  { bool slept = false; uint64_t us = 0;
    try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
    CHECK(slept, "beragadt gombnál elalszik");
    CHECK(us == 60ULL*1000000ULL, "60 másodperces ébresztés");
    CHECK(g_gpioWakeMask == 0, "gomb-ébresztés NINCS armolva (különben boot loop)"); }

}

static void sc10() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { // 5 sikertelen router reset -> tosleep()
    try {
      for (int c = 0; c < 10; c++) {
        int guard = 0;
        while (!reset_device() && ++guard < 200000) { yield(); }
      }
    } catch (DeepSleepSignal&) {}
    CHECK(g_pinState[PIN_RELAY] == LOW,  "relé (GPIO10) LOW - a router kap áramot");
    CHECK(g_pinState[6] == LOW,  "státusz LED (GPIO6) LOW");
    CHECK(g_pinState[5] == LOW,  "wifi LED (GPIO5) LOW");
    CHECK(!g_serialOn, "Serial.end() megtörtént");
    CHECK(logIndex(RELAY_LOW) < logIndex("DEEP_SLEEP"), "a relé az alvás ELŐTT kapcsolt le");
    CHECK(g_heldPins.count(PIN_RELAY) == 1, "a relé láb (GPIO10) hold-dal rögzítve alvásra");
    CHECK(g_deepSleepHoldEnabled, "a hold deep sleep alatt is érvényes (deep_sleep_hold_en)");
    { const int h = logIndex("gpio_hold_en(10)");
      CHECK(h >= 0 && h < logIndex("DEEP_SLEEP"), "a rögzítés az elalvás ELŐTT történt"); }
    CHECK(g_gpioWakeMask == (1ULL << 3), "reset gomb (GPIO3) ébresztésre armolva");
    CHECK(g_gpioWakeMode == 0, "LOW szintre ébred");
    CHECK(logIndex("timer_wakeup") < logIndex("DEEP_SLEEP"), "timer is armolva alvás előtt"); }

}

static void sc11() {
  // Az esp_deep_sleep_enable_gpio_wakeup() csak RTC-képes lábat fogad el;
  // az ESP32-C3-on ezek a GPIO0-GPIO5. Ha a firmware valaha nem RTC lábat
  // armolna, az API ESP_ERR_INVALID_ARG-gal elszállna a hardveren.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  try {
    for (int c = 0; c < 10; c++) {
      int guard = 0;
      while (!reset_device() && ++guard < 200000) { yield(); }
    }
  } catch (DeepSleepSignal&) {}
  CHECK(g_gpioWakeMask != 0, "armolt gomb-ébresztést");
  {
    bool allRtc = true;
    for (int gpio = 0; gpio < 64; gpio++)
      if ((g_gpioWakeMask >> gpio) & 1ULL) { if (gpio > 5) allRtc = false; }
    CHECK(allRtc, "minden armolt láb RTC-képes (GPIO0-5)");
  }

}

static void sc12() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_millis > 0, "friss boot");
  CHECK(deviceMode == (DeviceMode)0 && currentState == (State)0,
        "ébredés után TESTING_STATE / MONITOR (RTC memóriát nem használunk)");


  // ============ RELÉ / ROUTER RESET ============
}

static void sc13() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { g_log.clear();
    uint32_t t0 = g_millis;
    int guard = 0;
    while (!reset_device() && ++guard < 200000) { yield(); }
    uint32_t pulse = g_millis - t0;
    CHECK(guard < 200000, "reset_device() befejeződött");
    CHECK(pulse >= 90000, "a pulzus legalább 90s volt (nem ~0)");
    CHECK(pulse < 92000, "és nem lényegesen több");
    CHECK(logIndex(RELAY_HIGH) >= 0, "relé bekapcsolt (router áramtalanítva)");
    CHECK(logIndex(RELAY_HIGH) < logIndex(RELAY_LOW), "előbb be, aztán ki");
    CHECK(g_pinState[PIN_RELAY] == LOW, "a végén a router visszakapta az áramot"); }

}

static void sc14() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { int cycles = 0; bool slept = false;
    try {
      for (; cycles < 10; cycles++) {
        int guard = 0;
        while (!reset_device() && ++guard < 200000) { yield(); }
      }
    } catch (DeepSleepSignal&) { slept = true; }
    CHECK(slept, "elaludt");
    CHECK(cycles == 4, "4 tényleges reset után az 5. eseménynél alszik el"); }

  // ============ HTTP TESZT ============
}

static void sc15() {
  g_httpCode = 200; g_httpSize = -2; g_httpBeginOk = true;
  g_httpBody = "Microsoft NCSI";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "pontos egyezés");
  g_httpBody = "Microsoft NCSI\r\n";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "CRLF-fel a végén is egyezik");
  g_httpBody = "  Microsoft NCSI \n";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "körbevágott whitespace-szel is");

}

static void sc16() {
  g_httpBody = "Something Else";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "más tartalom -> false");
  g_httpBody = "Microsoft NCSI"; g_httpCode = 302;
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "302 átirányítás -> false");
  g_httpCode = 200; g_httpBeginOk = false;
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "begin() hiba -> false");
  g_httpBeginOk = true;

}

static void sc17() {
  { std::string big(50000, 'x');
    g_httpBody = big; g_httpSize = 50000;
    uint32_t t0 = g_millis;
    bool r = testInternetHTTP("http://x/", "Microsoft NCSI");
    CHECK(!r, "nagy válasz -> false");
    CHECK(g_millis - t0 < 500, "azonnal elutasítja, nem olvassa végig");
    g_httpSize = -2; }

}

static void sc18() {
  { g_httpBody = std::string(50000, 'y'); g_httpSize = -1;
    CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "nem egyezik, de nem is szállt el");
    g_httpSize = -2; g_httpBody = "Microsoft NCSI"; }

  // ============ PING TESZT ============
}

static void sc19() {
  pingSim = PingSim(); pingSim.ok = true;
  CHECK(testInternetPing(IPAddress(1, 1, 1, 1), "Test"), "sikeres");
  CHECK(pingSim.calls == 2, "csak 2 pinget futtatott a 4-ből (korai kilépés)");

}

static void sc20() {
  pingSim = PingSim(); pingSim.ok = false;
  CHECK(!testInternetPing(IPAddress(1, 1, 1, 1), "Test"), "sikertelen");
  CHECK(pingSim.calls == 3, "3 ping után eldőlt, a 4. felesleges lett volna");

  // ============ KONFIG I/O ============
}

static void sc21() {
  { g_fs.clear();
    char buf[33];
    writeConfigValue(LittleFS, "/t.txt", "MyNetwork");
    CHECK(readConfigValue(LittleFS, "/t.txt", buf, sizeof(buf)) == (ConfigStatus)0, "CONFIG_OK, ha van tartalom");
    CHECK(std::string(buf) == "MyNetwork", "az érték visszaolvasva");
    writeConfigValue(LittleFS, "/t.txt", "");
    CHECK(readConfigValue(LittleFS, "/t.txt", buf, sizeof(buf)) == (ConfigStatus)0, "üres fájl -> CONFIG_OK (nincs érték, de nem hiba)");
    CHECK(buf[0] == '\0', "és a buffer ki lett ürítve");
    CHECK(readConfigValue(LittleFS, "/nincs.txt", buf, sizeof(buf)) == (ConfigStatus)1, "hiányzó fájl -> CONFIG_MISSING");
    g_fs["/t.txt"] = "sor1\nsor2";
    readConfigValue(LittleFS, "/t.txt", buf, sizeof(buf));
    CHECK(std::string(buf) == "sor1", "csak az első sort olvassa");
    char small[5];
    g_fs["/t.txt"] = "abcdefghij";
    readConfigValue(LittleFS, "/t.txt", small, sizeof(small));
    CHECK(std::string(small) == "abcd", "kis bufferbe csonkít, nem csordul túl"); }

  // ============ GOMB DEBOUNCE ============
}

static void sc22() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { bool restarted = false;
    g_pinRead[3] = LOW;              // gomb lemegy
    try { resetbutton(); }           // első észlelés: csak megjegyzi
    catch (RestartSignal&) { restarted = true; }
    CHECK(!restarted, "az első LOW mintavétel nem indít újra");
    g_pinRead[3] = HIGH;             // tüske vége
    try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
    CHECK(!restarted, "felengedés után sem");
    // most tartós nyomás
    g_pinRead[3] = LOW;
    try { resetbutton(); g_millis += 100; resetbutton(); }
    catch (RestartSignal&) { restarted = true; }
    CHECK(restarted, "tartós (>50ms) nyomásra viszont újraindul"); }

}

static void sc23() {
  // A beallito urlap a PROGRAMBAN van (CONFIG_FORM), nem a LittleFS-en.
  // Barmilyen is a fajlrendszer allapota, az eszkoznek konfigurálhatónak
  // kell maradnia - korabban egy elfelejtett data/ feltoltes utan a portal a
  // tartalek urlapra esett vissza, most nincs mire visszaesni.
  coldBoot(false, "", "", "", "");
  g_fs.clear();                       // teljesen ures fajlrendszer
  try { setup(); } catch (DeepSleepSignal&) {}
  CHECK(deviceMode == (DeviceMode)1, "konfig portál elindult LittleFS tartalom nélkül is");

  AsyncWebServerRequest root; g_handlers["/#1"](&root);
  CHECK(root._code == 200, "a / 200-at ad");
  CHECK(root._body.find("name=\"ssid\"") != std::string::npos
        && root._body.find("method=\"POST\"") != std::string::npos,
        "kitölthető űrlap, nem hibaüzenet");
  CHECK(root._body.find("data/") == std::string::npos,
        "nincs benne a régi 'nincs feltöltve a data/' figyelmeztetés");
  CHECK(root._body.find("style.css") == std::string::npos
        && root._body.find("favicon") == std::string::npos,
        "az űrlap nem hivatkozik külső fájlra - egyetlen kérésből teljes");

  // BIZTONSAG: a statikus vegpontok NEM leteznek tobbe. Ez nem szepseghiba:
  // amig voltak, egy elirt utvonal-szabaly a /pass.txt-t is kiadhatta volna.
  CHECK(g_handlers.count("/style.css#1") == 0, "nincs /style.css útvonal");
  CHECK(g_handlers.count("/favicon.png#1") == 0, "nincs /favicon.png útvonal");

  // A /log viszont működik - épp ilyenkor kell a legjobban.
  AsyncWebServerRequest lg; g_handlers["/log#1"](&lg);
  CHECK(lg._code == 200, "a diagnosztikai napló elérhető");
}


static void scWDT1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_wdtEnabled, "a loop task fel van iratkozva a watchdogra");
  CHECK(g_wdtPanic, "trigger_panic = true (alapból csak figyelmeztetne!)");
  CHECK(g_wdtTimeoutMs == 90000, "timeout 90 mp");
  CHECK(g_wdtIdleMask == 0, "az idle taskot NEM figyeltetjük (hurok-veszély)");
  CHECK(logIndex("wdt_reconfigure") >= 0, "a már futó TWDT-t konfiguráltuk újra");
}

static void scWDT2() {
  // A legfontosabb: a 90 mp-es relé pulzus alatt sem maradhat a watchdog etetlen
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  int guard = 0;
  while (!reset_device() && ++guard < 200000) {
    resetbutton(); wifiresetbutton(); feedLoopWDT(); delay(10);
  }
  CHECK(guard < 200000, "a reset pulzus lefutott");
  CHECK(g_wdtMaxFeedGap < g_wdtTimeoutMs, "a 90 mp-es pulzus alatt végig etetve volt");
  CHECK(g_wdtMaxFeedGap <= 50, "az etetési köz ~10 ms nagyságrendű");
}

static void scWDT3() {
  // A leghosszabb saját blokkolás: reconnectWifi() 3x20s timeout + 2x20s várakozás
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  wifiSim.willConnect = false; wifiSim.begun = false;
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  try { reconnectWifi(); } catch (DeepSleepSignal&) {}
  CHECK(g_wdtMaxFeedGap < g_wdtTimeoutMs, "a ~100 mp-es újracsatlakozás alatt is etetve volt");
  CHECK(g_wdtMaxFeedGap <= 50, "etetési köz ~10 ms");
}

static void scWDT4() {
  // A hosszú várakozások delay()-t használnak, nem yield()-pörgetést
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_log.clear();
  const uint32_t t0 = g_millis;
  waitWithButtons(1000);
  CHECK(g_millis - t0 >= 1000, "a várakozás ténylegesen eltelt");
  int feeds = 0;
  for (auto& l : g_log) if (l == "wdt_feed") feeds++;
  CHECK(feeds >= 90 && feeds <= 110, "~100 etetés 1 mp alatt (10 ms-os osztás)");
}


static void scFS1() {
  // Nem csatolható fájlrendszer -> fsReady = false, és végzetes hiba
  // (a részletes viselkedést az FT1 fedi)
  coldBoot(false, "", "", "", "");
  g_fsMountOk = false;
  try { setup(); } catch (DeepSleepSignal&) { CHECK(false, "nem szabadna elaludnia"); }
  CHECK(!fsReady, "fsReady = false");
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL - mentés úgysem lenne lehetséges");
  CHECK(g_wdtEnabled, "a watchdog hibajelzés közben is aktív");
}

static void scFS2() {
  // Írásra nem nyitható fájlrendszer
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fsWritable = false;
  CHECK(!writeConfigValue(LittleFS, "/x.txt", "adat"), "sikertelen megnyitás -> false");
  g_fsWritable = true;
  CHECK(writeConfigValue(LittleFS, "/x.txt", "adat"), "működő FS-en -> true");
}

static void scFS3() {
  // Megtelt fájlrendszer: a rövid írást el kell kapni
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fs.clear();
  g_fsCapacity = 8;
  CHECK(!writeConfigValue(LittleFS, "/x.txt", "ez tobb mint nyolc bajt"),
        "rövid írás -> false (nem hazudik sikert)");
  CHECK(writeConfigValue(LittleFS, "/x.txt", "rovid"), "ami befér, az sikerül");
  g_fsCapacity = 0;
}

static void scFS4() {
  // A visszaolvasásos ellenőrzés akkor is fog, ha az írás "sikeresnek" tűnt,
  // de a tartalom mégsem került ki (pl. lezáráskori hiba).
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(writeConfigValue(LittleFS, "/v.txt", "helyes"), "normál írás rendben");
  g_fs["/v.txt"] = "serult";   // valaki más elrontja a tartalmat
  CHECK(!fileMatches(LittleFS, "/v.txt", "helyes", 6), "az ellenőrzés kiszúrja az eltérést");
}

static void scFS5() {
  // Törlés: ha a csonkolás nem megy, a fájl törlésére vált
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fs["/c.txt"] = "valami";
  g_fsWritable = false;          // csonkolni nem lehet
  g_fsRemoveOk = true;           // törölni igen
  CHECK(clearConfigValue(LittleFS, "/c.txt"), "a remove() tartalékra vált");
  CHECK(!g_fs.count("/c.txt"), "a fájl tényleg eltűnt");

  g_fs["/d.txt"] = "valami";
  g_fsRemoveOk = false;          // most semmi sem megy
  CHECK(!clearConfigValue(LittleFS, "/d.txt"), "ha egyik sem megy, false-t ad");
  g_fsWritable = true; g_fsRemoveOk = true;
}

static void scFS6() {
  // Hiányzó és könyvtár-jellegű bemenet olvasáskor
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  char buf[33];
  memset(buf, 'X', sizeof(buf));
  CHECK(readConfigValue(LittleFS, "/nincs_ilyen.txt", buf, sizeof(buf)) == (ConfigStatus)1, "hiányzó fájl -> CONFIG_MISSING");
  CHECK(buf[0] == '\0', "a buffer akkor is ki lett ürítve");
}


static void scFT1() {
  // Csatolhatatlan fájlrendszer -> végzetes hiba, NEM AP portál
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  CHECK(wifiSim.softApCount == 0, "NEM indult AP portál");
  CHECK(wifiSim.beginCount == 0, "meg sem próbált csatlakozni");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scFT2() {
  // A fájl létezik, de nem olvasható -> végzetes hiba
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsReadable = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  CHECK(wifiSim.softApCount == 0, "NEM indult AP portál");
}

static void scFT3() {
  // ELSŐ INDÍTÁS: nincs egyetlen konfig fájl sem -> ez NEM hiba
  coldBoot(false, "", "", "", "");
  g_fs.clear();
  setup();
  CHECK(deviceMode == (DeviceMode)1, "MODE_CONFIG - AP portál, nem hibajelzés");
  CHECK(wifiSim.softApCount == 1, "elindult az AP");
}

static void scFT4() {
  // WIFIRESET UTÁN: a fájlok léteznek, de üresek -> ez sem hiba
  coldBoot(false, "", "", "", "");
  g_fs["/ssid.txt"] = ""; g_fs["/pass.txt"] = "";
  g_fs["/ip.txt"] = "";   g_fs["/gateway.txt"] = "";
  setup();
  CHECK(deviceMode == (DeviceMode)1, "MODE_CONFIG - AP portál, nem hibajelzés");
  CHECK(wifiSim.softApCount == 1, "elindult az AP");
}

static void scFT5() {
  // A villogás: mindkét LED EGYSZERRE, gyorsan
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  g_log.clear();
  int toggles = 0;
  const uint32_t t0 = g_millis;
  while (g_millis - t0 < 1000) {
    const int before = g_pinState[6];
    loop();
    if (g_pinState[6] != before) {
      toggles++;
      CHECK(g_pinState[6] == g_pinState[5], "a két LED mindig azonos fázisban");
      if (toggles > 2) break;   // ne árasszuk el a kimenetet
    }
  }
  CHECK(toggles >= 2, "villog (1 mp alatt többször váltott)");

  // periódus ellenőrzése: 100 ms-onként vált -> 1 mp alatt ~10 váltás
  g_pinState[6] = LOW; g_pinState[5] = LOW;
  int t2 = 0;
  const uint32_t t1 = g_millis;
  while (g_millis - t1 < 1000) {
    const int before = g_pinState[6];
    loop();
    if (g_pinState[6] != before) t2++;
  }
  CHECK(t2 >= 8 && t2 <= 12, "~10 váltás másodpercenként (100 ms-os félperiódus)");
}

static void scFT6() {
  // Hibajelzés közben is működjenek a gombok, és NE fusson az állapotgép
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  for (int i = 0; i < 50; i++) loop();
  CHECK(pingSim.calls == 0, "nem futott internet teszt");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé végig LOW maradt");

  bool restarted = false;
  g_pinRead[3] = LOW;
  try { loop(); g_millis += 100; loop(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a reset gomb hibajelzés közben is újraindít");
}


static void scFT7() {
  // 5 perc hibajelzés után alvás - IDŐZÍTETT ÉBRESZTÉS NÉLKÜL
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");

  bool slept = false; uint64_t us = 1;
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 6u * 60 * 1000) loop();
  } catch (DeepSleepSignal& d) { slept = true; us = d.us; }

  CHECK(slept, "elaludt");
  const uint32_t elapsed = g_millis - t0;
  CHECK(elapsed >= 5u * 60 * 1000, "csak 5 perc után (nem korábban)");
  CHECK(elapsed < 5u * 60 * 1000 + 2000, "és nem sokkal később");
  CHECK(us == 0, "NINCS időzített ébresztés - magától nem ébred fel");
  CHECK(g_gpioWakeMask == (1ULL << 3), "a reset gomb viszont felébreszti");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot alvás közben is");
  CHECK(logIndex("wakeup_disable_all") < logIndex("DEEP_SLEEP"),
        "előbb minden ébresztőforrást töröltünk");
}

static void scFT8() {
  // Az internet-hiba miatti elalvás (5 sikertelen router reset) TOVÁBBRA IS
  // időzítve ébred - ott a WiFi működik, csak az internet nem.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  uint64_t us = 0;
  try {
    for (int c = 0; c < 10; c++) {
      int guard = 0;
      while (!reset_device() && ++guard < 200000) { yield(); }
    }
  } catch (DeepSleepSignal& d) { us = d.us; }
  CHECK(us == 3600ULL * 1000000ULL, "1 órás ébresztés megmaradt az internet-hiba útvonalon");
}


static void scWF1() {
  // Van mentett SSID, de a router nem elérhető: NEM megy azonnal AP módba
  coldBoot(false, "MyNetwork", "titok123", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "MONITOR mód - kivárja a first start delayt");
  CHECK(wifiSim.softApCount == 0, "nem indult AP portál");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scWF2() {
  // Egy teljes kör: 10 perc várakozás + 3 próba + router reset + 10 perc
  // bootolási várakozás + 3 próba, és csak ezután 1 órás alvás
  coldBoot(false, "MyNetwork", "titok123", "", "");
  setup();
  const int beginBefore = wifiSim.beginCount;
  const uint32_t t0 = g_millis;
  bool slept = false; uint64_t us = 0;
  try { while (g_millis - t0 < 20u*60*1000 && deviceMode == (DeviceMode)0) loop(); }
  catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt (nem AP módba ment)");
  CHECK(us == 3600ULL*1000000ULL, "1 óra múlva magától ébred és újrapróbálja");
  CHECK(rtcRetryRounds == 1, "egy kör elkönyvelve");
  const int begins = wifiSim.beginCount - beginBefore;
  const uint32_t dt = g_millis - t0;
  CHECK(begins >= 6, "megvan a 3 + 3 blokkoló csatlakozási kísérlet");
  // A tobbi begin() az onlineProbe() 60 mp-enkenti ebresztgetese a ket hosszu
  // varakozas alatt. A FELSO KORLAT maga a szerzodes: percenkent legfeljebb
  // egy begin(), tehat sem a radio, sem (a persistent(false) miatt) a flash
  // nem kap tobbet, mint amennyit a dokumentacio iger.
  CHECK(begins <= 6 + (int)(dt / 60000) + 2,
        "a proba percenkent legfeljebb egy begin()-t ad hozza");
  CHECK(logIndex(RELAY_HIGH) >= 0, "a körben lefutott egy router újraindítás");
  CHECK(dt >= 10u*60*1000, "megvárta a 10 perces first start delayt");
  CHECK(dt >= 25u*60*1000, "és a reset + bootolási várakozást is (~25,5 perc)");
  CHECK(dt < 27u*60*1000, "de nem sokkal többet");
}

static void scWF3() {
  // AP mód 5 perc után elalszik, időzített ébresztés nélkül
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP mód");
  bool slept = false; uint64_t us = 1;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 8u*60*1000) loop(); }
  catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt");
  const uint32_t dt = g_millis - t0;
  CHECK(dt >= 5u*60*1000 && dt < 5u*60*1000 + 2000, "pontosan 5 perc után");
  CHECK(us == 0, "NINCS időzített ébresztés");
  CHECK(g_gpioWakeMask == (1ULL << 3), "a reset gomb ébreszti");
}

static void scWF4() {
  // Fájlírás közben SOHA nem alszik el (utolsó pillanatban beírt adatok)
  coldBoot(false, "", "", "", "");
  setup();
  savingConfig = true;
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 8u*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "mentés közben NEM aludt el, pedig letelt az 5 perc");
  savingConfig = false;
  try { loop(); } catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "a mentés befejeztével viszont elalszik");
}

static void scWF5() {
  // Minden HTTP kérés újraindítja az 5 perces visszaszámlálást
  coldBoot(false, "", "", "", "");
  setup();
  const uint32_t first = apDeadline;
  const uint32_t t0 = g_millis;
  while (g_millis - t0 < 4u*60*1000) loop();
  touchApDeadline();
  CHECK(apDeadline > first, "a kérés kitolta a határidőt");
  bool slept = false;
  const uint32_t t1 = g_millis;
  try { while (g_millis - t1 < 4u*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "a kérés után újabb teljes időablakot kapott");
}

static void scWF6() {
  // Működés közbeni kapcsolatvesztés: 3 próba, majd AZONNAL router reset
  coldBoot(true, "MyNetwork", "pw", "", "");
  setup();
  loop();  // a firstStart fázis lezárása, innentől valódi monitorozás
  CHECK(deviceMode == (DeviceMode)0, "monitor módban vagyunk");
  const int beginBefore = wifiSim.beginCount;
  wifiSim.willConnect = false; wifiSim.begun = false;   // megszakad a kapcsolat
  g_log.clear();
  int guard = 0;
  try { while (logIndex(RELAY_HIGH) < 0 && ++guard < 200000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(wifiSim.beginCount - beginBefore == 3, "3 újrapróbálkozás a reset előtt");
  CHECK(logIndex(RELAY_HIGH) >= 0, "elindult a router újraindítás (relé be)");
}



static void scWD1() {
  // Egy watchdog reset még nem végzetes
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0; rtcWdtResets = 0;      // friss bekapcsolás
  g_resetReason = ESP_RST_TASK_WDT;
  setup();
  CHECK(rtcWdtResets == 1, "első watchdog reset elkönyvelve");
  CHECK(deviceMode == (DeviceMode)0, "normálisan fut tovább");
}

static void scWD2() {
  // A 3. watchdog reset után végzetes hibajelzés
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0; rtcWdtResets = 0;
  g_resetReason = ESP_RST_TASK_WDT;
  setup();
  CHECK(rtcWdtResets == 1, "1. reset");

  coldBoot(true, "TestNet", "pw", "", "");   // a NOINIT tartalom megmarad
  g_resetReason = ESP_RST_TASK_WDT;
  setup();
  CHECK(rtcWdtResets == 2, "2. reset");

  coldBoot(true, "TestNet", "pw", "", "");
  g_resetReason = ESP_RST_TASK_WDT;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "3. reset -> MODE_FATAL");
  CHECK(wifiSim.softApCount == 0, "nem indult AP portál");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scWD3() {
  // A végzetes állapot 5 perc után alszik, időzített ébresztés nélkül
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0; rtcWdtResets = 2;       // már volt két reset
  g_resetReason = ESP_RST_TASK_WDT;
  rtcWdtMagic = 0x42415A53UL;              // érvényes számláló
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  bool slept = false; uint64_t us = 1;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 8u*60*1000) loop(); }
  catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt");
  CHECK(us == 0, "NINCS időzített ébresztés - csak gombbal ébred");
  CHECK(g_gpioWakeMask == (1ULL << 3), "a reset gomb ébreszti");
  CHECK(rtcWdtResets == 0, "a számláló nullázva, hogy ébredés után tiszta lap legyen");
}

static void scWD4() {
  // Áramtalanítás / külső reset nullázza a számlálót
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0x42415A53UL; rtcWdtResets = 2;
  g_resetReason = ESP_RST_POWERON;
  rtcRetryRounds = 0;
  g_gpioWakeResult = 0;
  setup();
  CHECK(rtcWdtResets == 0, "bekapcsolás -> tiszta lap");
  CHECK(deviceMode == (DeviceMode)0, "normálisan fut");
}

static void scWD5() {
  // Szoftveres újraindítás (reset gomb) NEM számít watchdog hibának,
  // de nem is nulláz - a beragadt hiba így nem tüntethető el véletlenül.
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0x42415A53UL; rtcWdtResets = 2;
  g_resetReason = ESP_RST_SW;
  setup();
  CHECK(rtcWdtResets == 2, "szoftveres reset nem növel és nem nulláz");
  CHECK(deviceMode == (DeviceMode)0, "normálisan fut");
}

static void scWD6() {
  // 1 óra hibátlan működés után a számláló nullázódik
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0x42415A53UL; rtcWdtResets = 2;
  g_resetReason = ESP_RST_SW;
  g_httpBody = "Microsoft Connect Test";   // a tesztek sikeresek -> egészséges futás
  setup();
  CHECK(rtcWdtResets == 2, "két korábbi reset még számon van tartva");
  // Egy órányi hibátlan futás. Az időt előreugratjuk: a nullázás a loop()
  // elején történik, de a firstStart fázist előbb le kell zárni.
  loop();                       // firstStart lezárása
  g_millis = 61u * 60 * 1000;
  loop();                       // most fut le a nullázás
  CHECK(rtcWdtResets == 0, "1 óra hibátlan működés után nullázódott");
}


static void scE1() {
  // Teljes egészséges ciklus: boot -> teszt -> SUCCESS -> újabb teszt
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  loop();                                  // firstStart lezárása
  CHECK(deviceMode == (DeviceMode)0, "monitor mód");
  loop();                                  // 1. teszt
  CHECK(currentState == (State)2, "sikeres teszt -> SUCCESS_STATE");
  CHECK(g_pinState[5] == HIGH, "wifi LED világít");
  const uint32_t t0 = g_millis;
  int guard = 0;
  while (currentState == (State)2 && ++guard < 100000) { loop(); g_millis += 10; }
  CHECK(g_millis - t0 >= 60000, "SUCCESS_DELAY teljes 1 perc volt");
  CHECK(g_millis - t0 < 62000, "és nem több");
}

static void scE2() {
  // Internet kiesik, WiFi jó: 4 kör teszt -> router reset -> 10 perc -> vissza
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Valami mas";               // minden HTTP teszt bukik
  pingSim.ok = false;                      // a pingek is
  setup();
  loop();
  g_log.clear();
  int guard = 0;
  while (logIndex(RELAY_HIGH) < 0 && ++guard < 300000) { loop(); g_millis += 10; }
  CHECK(guard < 300000, "eljutott a router resetig");
  const int relayOn = logIndex(RELAY_HIGH);
  const int relayOff = logIndex(RELAY_LOW);
  CHECK(relayOn >= 0 && relayOff > relayOn, "relé be, majd ki");
  // a reset után visszatér tesztelni
  guard = 0;
  while (currentState != (State)0 && ++guard < 300000) { loop(); g_millis += 10; }
  CHECK(guard < 300000, "visszatért TESTING_STATE-be a reset után");
  CHECK(deviceMode == (DeviceMode)0, "monitor módban maradt (a WiFi jó volt)");
}

static void scE3() {
  // Router reset után a WiFi sem jön vissza -> AP mód
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  loop();                                   // firstStart lezárása, monitor mód
  CHECK(deviceMode == (DeviceMode)0, "monitor mód");
  wifiSim.willConnect = false; wifiSim.begun = false;   // elvágjuk a WiFi-t
  g_log.clear();
  int guard = 0;
  bool slept = false;
  try { while (deviceMode == (DeviceMode)0 && ++guard < 500000) { loop(); g_millis += 10; } }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "a router reset után sem jött vissza -> alvás és újrapróbálkozás");
  CHECK(deviceMode == (DeviceMode)0, "NEM AP mód (a hálózat csak nem látszik)");
  CHECK(rtcRetryRounds == 1, "egy újrapróbálkozási kör elkönyvelve");
  CHECK(logIndex(RELAY_HIGH) >= 0, "közben lefutott egy router újraindítás");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé a végén LOW - a router kap áramot");
}

static void scE4() {
  // startConfigPortal() ismételt hívása nem duplikálja az AP-t
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(wifiSim.softApCount == 1, "egyszer indult");
  startConfigPortal();
  startConfigPortal();
  CHECK(wifiSim.softApCount == 1, "ismételt hívás nem indít újabb AP-t");
}

static void scE5() {
  // Wifireset gomb AP módban: törli a mentett adatokat és újraindít
  coldBoot(false, "RegiHalozat", "regijelszo", "", "");
  setup();   // nem sikerul csatlakozni -> monitor mod, majd handleFirstStart
  g_fs["/ssid.txt"] = "RegiHalozat";
  bool restarted = false;
  g_pinRead[2] = LOW;                      // D0 = GPIO2 wifireset
  try { loop(); g_millis += 100; loop(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a wifireset gomb újraindított");
  CHECK(g_fs["/ssid.txt"].empty() || !g_fs.count("/ssid.txt"),
        "a mentett SSID törölve lett");
}


// A regisztrált POST handler meghajtása egy hamis kéréssel
static int postConfig(const char* ssidVal, const char* passVal,
                      const char* ipVal, const char* gwVal,
                      std::string* body = nullptr) {
  AsyncWebServerRequest req;
  if (ssidVal) req.addParam("ssid", ssidVal);
  if (passVal) req.addParam("pass", passVal);
  if (ipVal)   req.addParam("ip", ipVal);
  if (gwVal)   req.addParam("gateway", gwVal);
  g_handlers["/#2"](&req);
  if (body) *body = req._body;
  return req._code;
}

static void scP1() {
  // Érvényes mentés: 200, fájlok kiírva, újraindítás beütemezve
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP mód");
  const int code = postConfig("MyNetwork", "titkosjelszo", "", "");
  CHECK(code == 200, "HTTP 200");
  CHECK(g_fs["/ssid.txt"] == "MyNetwork", "SSID kiírva");
  CHECK(storedPass() == "titkosjelszo", "a jelszó visszaolvasva egyezik");
  CHECK(g_fs["/pass.txt"].find("titkosjelszo") == std::string::npos,
        "de a fájlban NEM szerepel nyílt szöveggel");
  CHECK(g_fs["/ip.txt"].empty(), "üres IP -> DHCP");
  CHECK(restartPending, "újraindítás beütemezve");
  CHECK(!savingConfig, "a mentés jelző visszaállt");
}

static void scP2() {
  // Túl hosszú SSID: NEM szabad sikert jelenteni és NEM szabad újraindulni
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const char* longSsid = "012345678901234567890123456789012345";  // 36 karakter
  const int code = postConfig(longSsid, "jelszo", "", "", &body);
  CHECK(code == 500, "HTTP 500 - nem hazudik sikert");
  CHECK(!restartPending, "NEM indul újra, az adatok nem vesznek el");
  CHECK(body.find("SSID") != std::string::npos, "a válasz megnevezi az okot");
}

static void scP3() {
  // Hiányzó SSID: szintén hiba
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig(nullptr, "jelszo", "", "");
  CHECK(code == 500, "HTTP 500 hiányzó SSID esetén");
  CHECK(!restartPending, "nem indul újra");
}

static void scP4() {
  // Érvénytelen IP formátum. A mentés két fázisú: hibás mezőnél az ELŐTTE
  // álló érvényes mezők SEM íródnak ki - különben fél-új, fél-régi
  // konfiguráció maradna a flashben.
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("MyNetwork", "jelszo", "nem-ip-cim", "", &body);
  CHECK(code == 500, "HTTP 500 rossz IP esetén");
  CHECK(!restartPending, "nem indul újra");
  CHECK(body.find("IP") != std::string::npos, "a válasz megnevezi az okot");
  CHECK(!g_fs.count("/ssid.txt") && !g_fs.count("/pass.txt"),
        "az érvényes SSID/jelszó SEM íródott ki (két fázisú mentés)");
  CHECK(ssid[0] == '\0', "a futó konfiguráció (globálisok) is érintetlen");
}

static void scP5() {
  // Írásra képtelen fájlrendszer: 500, nincs újraindulás
  coldBoot(false, "", "", "", "");
  setup();
  g_fsWritable = false;
  const int code = postConfig("MyNetwork", "jelszo", "", "");
  CHECK(code == 500, "HTTP 500 írási hiba esetén");
  CHECK(!restartPending, "nem indul újra");
  g_fsWritable = true;
}

static void scP6() {
  // Statikus IP mentése, és a jelszó nem szivárog ki
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("MyNetwork", "SzuperTitok", "192.168.1.200",
                              "192.168.1.1", &body);
  CHECK(code == 200, "HTTP 200");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200", "statikus IP kiírva");
  CHECK(g_fs["/gateway.txt"] == "192.168.1.1", "gateway kiírva");
  CHECK(body.find("SzuperTitok") == std::string::npos,
        "a jelszó NEM jelenik meg a válaszban");
  CHECK(body.find("192.168.1.200") != std::string::npos,
        "az IP viszont igen, hogy tudja hova menjen");
}

static void scP7() {
  // A GET oldal kiszolgálása és a határidő kitolása
  coldBoot(false, "", "", "", "");
  setup();
  const uint32_t before = apDeadline;
  g_millis += 60000;
  AsyncWebServerRequest req;
  g_handlers["/#1"](&req);                      // HTTP_GET "/"
  CHECK(req._code == 200, "a beállító oldal kiszolgálva");
  CHECK(apDeadline > before, "a kérés kitolta az 5 perces határidőt");
}


static void scCPU1() {
  // A loop() a várakozó állapotokban átadja a CPU-t (delay), nem pörög.
  // Enélkül a SUCCESS_STATE 1 percig 100%-on járatná a processzort.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  loop();                       // firstStart lezárása
  loop();                       // teszt -> SUCCESS_STATE
  CHECK(currentState == (State)2, "SUCCESS_STATE");
  const uint32_t before = g_millis;
  loop();
  CHECK(g_millis > before, "a loop() ténylegesen várakozik (delay), nem pörög");
  CHECK(g_millis - before <= 20, "de csak ~10 ms-ot, a válaszkészség megmarad");
}

static void scCPU2() {
  // A 10 perces first start várakozás alatt sem pörög
  coldBoot(false, "MyNetwork", "pw", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "monitor mód, first start várakozás");
  const uint32_t before = g_millis;
  loop();
  CHECK(g_millis > before, "a várakozás delay-jel megy");
  CHECK(g_millis - before <= 20, "10 ms-os szemcsézettség");
}


static void scX1() {
  // Reset gomb megnyomása a 90 mp-es relé pulzus KÖZBEN.
  // A router ilyenkor áram nélkül van - az újraindulásnak vissza kell adnia.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  bool restarted = false;
  int guard = 0;
  try {
    // elindítjuk a resetet, majd a pulzus alatt lenyomjuk a gombot
    while (!reset_device() && ++guard < 200000) {
      if (guard == 50) g_pinRead[3] = LOW;      // D1 lenyomva
      resetbutton(); wifiresetbutton(); delay(10);
    }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a gomb újraindított a pulzus közben");
  CHECK(g_pinState[PIN_RELAY] == HIGH, "a relé ekkor még HIGH volt (router áram nélkül)");
  // az újraindulás után a setup() azonnal áramot ad a routernek
  g_pinRead[3] = HIGH;
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_pinState[PIN_RELAY] == LOW, "az újraindulás után a relé LOW - a router kap áramot");
}

static void scX2() {
  // Nyílt hálózat: üres jelszó érvényes
  coldBoot(false, "", "", "", "");
  setup();
  AsyncWebServerRequest req;
  req.addParam("ssid", "NyiltHalozat");
  req.addParam("pass", "");
  req.addParam("ip", ""); req.addParam("gateway", "");
  g_handlers["/#2"](&req);
  CHECK(req._code == 200, "üres jelszó elfogadva (nyílt hálózat)");
  CHECK(g_fs["/ssid.txt"] == "NyiltHalozat", "SSID mentve");
  CHECK(g_fs.count("/pass.txt") && storedPass().empty(), "üres jelszó mentve");
}

static void scX3() {
  // Határértékek: pontosan 32 karakteres SSID és 63 karakteres jelszó
  coldBoot(false, "", "", "", "");
  setup();
  std::string s32(32, 'A'), s63(63, 'B');
  const int code = postConfig(s32.c_str(), s63.c_str(), "", "");
  CHECK(code == 200, "a pontos maximum még elfogadott");
  CHECK(g_fs["/ssid.txt"].size() == 32, "32 karakteres SSID hiánytalanul mentve");
  CHECK(storedPass().size() == 63, "63 karakteres jelszó hiánytalanul mentve");

  // egy karakterrel több már nem
  coldBoot(false, "", "", "", "");
  setup();
  std::string s33(33, 'A');
  CHECK(postConfig(s33.c_str(), "pw", "", "") == 500, "33 karakter már elutasítva");
}

static void scX4() {
  // Fél-konfigurált statikus IP: van IP, nincs gateway -> DHCP-re esik vissza
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "csatlakozott");
  CHECK(wifiSim.configCount == 0, "nem alkalmazott hiányos statikus configot");
  CHECK(!wifiSim.staticApplied, "DHCP-t használ");
}

static void scX5() {
  // Csak whitespace-t tartalmazó konfig fájl -> nincs érték, AP mód
  coldBoot(false, "", "", "", "");
  g_fs["/ssid.txt"] = "   \r\n";
  g_fs["/pass.txt"] = "";
  setup();
  CHECK(ssid[0] == '\0', "a whitespace levágás után üres az SSID");
  CHECK(deviceMode == (DeviceMode)1, "AP mód, nem végzetes hiba");
}

static void scX6() {
  // Sikertelen mentés után is kitolódik az AP határidő (van idő újrapróbálni)
  coldBoot(false, "", "", "", "");
  setup();
  g_millis += 4u * 60 * 1000;                 // majdnem lejárt
  const uint32_t before = apDeadline;
  postConfig("Halozat", "pw", "rossz-ip", "");   // validáció bukik
  CHECK(apDeadline > before, "a sikertelen mentés is kitolta a határidőt");
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 4u*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "van ideje kijavítani az adatokat");
}


// A router X perc mulva jelenik meg. Visszaadja: sikerult-e csatlakozni,
// es hany perc mulva dolt el a dolog.
static bool routerAppearsAt(uint32_t appearMs, uint32_t* decidedAtMs) {
  coldBoot(false, "MyNetwork", "titok123", "", "");
  // A rádió maga tudja, mikortól elérhető a hálózat - így a blokkoló
  // hívásokon belül is helyesen viselkedik.
  wifiSim.willConnect = true;
  wifiSim.availableFrom = appearMs;
  g_serialLog.clear();
  setup();
  int guard = 0;
  bool slept = false;
  try {
    while (++guard < 2000000) {
      if (deviceMode != (DeviceMode)0) break;                 // AP modba ment
      if (serialHas("Beginning Test.")) break;   // elkezdett monitorozni
      loop();
    }
  } catch (DeepSleepSignal&) { slept = true; }
  *decidedAtMs = g_millis;
  return !slept && deviceMode == (DeviceMode)0 && WiFi.status() == WL_CONNECTED;
}

static void scPO1() {
  // ÁRAMSZÜNET: a router 8 perc mulva all fel -> az eszkoznek ki kell varnia
  uint32_t at = 0;
  const bool ok = routerAppearsAt(8u * 60 * 1000, &at);
  printf("     [info] dontes %u perckor\n", at / 60000);
  CHECK(ok, "8 perces router indulást kivár és csatlakozik");
  CHECK(deviceMode == (DeviceMode)0, "monitor módban működik tovább");
}

static void scPO2() {
  // A router csak 11 perc mulva all fel - a first start delay alatt nem, de az
  // ezt koveto 3 probalkozas alatt igen
  uint32_t at = 0;
  const bool ok = routerAppearsAt(11u * 60 * 1000, &at);
  printf("     [info] dontes %u perckor\n", at / 60000);
  CHECK(ok, "11 percnél is elkapja az újrapróbálkozási ablakban");
}

static void scPO3() {
  // A router 20 perc mulva all fel - ez mar tul keso, AP modba megy
  uint32_t at = 0;
  bool ok = false;
  try { ok = routerAppearsAt(20u * 60 * 1000, &at); } catch (DeepSleepSignal&) {}
  printf("     [info] dontes %u perckor, mode=%d\n", at / 60000, (int)deviceMode);
  // A router reset utani bootolasi varakozas (10 perc) atnyulik a 20 percen,
  // igy az ezt koveto probalkozas mar elkapja - meg az ELSO korben.
  CHECK(ok, "a router reset utani próbálkozás elkapja, még az első körben");
  CHECK(deviceMode == (DeviceMode)0, "monitor módban működik tovább");
}


static void scR1() {
  // Rossz jelszó (hitelesítési hiba) -> AZONNAL AP mód, nem 2 nap várakozás.
  // authFail, nem failStatus: a core az elso disconnect-nel meg nem ad
  // WL_CONNECT_FAILED-et (STA.cpp:148), tehat a hu modell szigorubb.
  coldBoot(false, "MyNetwork", "rosszjelszo", "", "");
  wifiSim.authFail = true;
  setup();
  g_log.clear();
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 20u*60*1000 && deviceMode == (DeviceMode)0) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "nem aludt el");
  CHECK(deviceMode == (DeviceMode)1, "AP beállító módba ment");
  CHECK(rtcRetryRounds == 0, "nem számolt újrapróbálkozási kört");
  CHECK(logIndex(RELAY_HIGH) < 0, "rossz jelszónál NEM indítja újra a routert");
  // A setup() egyetlen probat tesz; a dontes csak a handleFirstStart() utani
  // 3 probat kovetoen szuletik meg. Ez nem veletlen: a core az elso disconnect
  // esemenynel meg WL_DISCONNECTED-et ad (STA.cpp:148), tehat EGYETLEN
  // probalkozasbol a rossz jelszo felismerhetetlen lenne.
  printf("     [info] csatlakozasi probalkozasok az AP modig: %d\n", wifiSim.beginCount);
  CHECK(wifiSim.beginCount >= 2, "egynel tobb probalkozas kellett a felismereshez");
}

// A rossz jelszo felismerese a core egy nem dokumentalt reszletetol fugg:
// az ELSO disconnect esemenynel a status meg WL_DISCONNECTED (STA.cpp:148,
// "AUTH_FAIL && !first_connect"), es csak a masodiktol WL_CONNECT_FAILED.
// Ezert nem eleg egyetlen probalkozas - ezt a fuggest rogziti ez az eset,
// hogy egy kesobbi "eleg egy proba is" egyszerusites ne csuszhasson at.
static void scR5() {
  coldBoot(false, "MyNetwork", "rosszjelszo", "", "");
  wifiSim.authFail = true;
  WiFi.begin("MyNetwork", "rosszjelszo");
  CHECK(WiFi.status() != WL_CONNECT_FAILED,
        "egyetlen probalkozas utan a rossz jelszo meg lathatatlan");
  WiFi.begin("MyNetwork", "rosszjelszo");
  CHECK(WiFi.status() == WL_CONNECT_FAILED,
        "a masodik probalkozastol viszont megkulonboztetheto");
}

// A README/MUKODES konkret szamot igér az ujraprobalkozasi korre: 25,5 perc
// ebren + 60 perc alvas = 85,5 perc, es 33 kor = 47 ora. Az ebren toltott reszt
// meg lehet merni - ha barmelyik idozites elmozdul, ez a teszt bukik, nem a
// dokumentacio rohad el csendben.
static void scR6() {
  coldBoot(false, "MyNetwork", "titok123", "", "");   // a halozat nincs ott
  setup();
  const uint32_t t0 = 0;                              // a boot a 0. ms
  bool slept = false;
  try { while (g_millis < 3u*60*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  const uint32_t awake = g_millis - t0;
  printf("     [info] boot -> deep sleep: %.1f perc\n", awake/60000.0);
  CHECK(slept, "egy ora alvasra ment, nem AP modba");
  CHECK(awake > 25u*60*1000 && awake < 26u*60*1000,
        "az ebren toltott resz 25 es 26 perc kozott van (a doksi 25,5-ot ir)");
}

// A doksi ket konkret felismerelesi idot igér: 123 mp, ha a nevek feloldodnak
// es csak a szerverek hallgatnak (5 x 15 mp + 4 x 12 mp), es 213 mp halott DNS
// mellett (5 x 33 mp + 4 x 12 mp). Meressel ellenorizzuk, kulonben az elso
// idozites-valtoztatasnal a README csendben hazudni kezdene.
//
// A relet NEM a g_log-bol figyeljuk: a reset_device() a 90 mp-es pulzust
// blokkolva futtatja le, tehat mire a ciklus visszakapja a vezerlest, a
// szimulalt ora mar 90 mp-cel tovabb jar. A soros napló "Uptime: XhYmZs" sorai
// viszont pontosan azt a pillanatot rogzitik, amikor a dontes megszuletett.
static int uptimeSecAt(size_t idx) {
  // a legkozelebbi MEGELOZO "Uptime: 0h 2m 4s" sor masodpercben
  for (size_t i = idx + 1; i-- > 0;) {
    int h, m, sec;
    if (sscanf(g_serialLog[i].c_str(), "Uptime: %dh %dm %ds", &h, &m, &sec) == 3) {
      return h * 3600 + m * 60 + sec;
    }
    if (i == 0) break;
  }
  return -1;
}

static void scR7() {
  auto merd = [](uint32_t failMs) -> int {
    coldBoot(true, "MyNetwork", "titok123", "", "");
    setup();
    loop();                       // a firstStart lezarasa
    g_httpCode = -1;              // innentol minden teszt bukik
    g_httpFailMs = failMs;
    g_serialLog.clear(); g_log.clear();
    int guard = 0;
    try { while (logIndex(RELAY_HIGH) < 0 && ++guard < 500000) loop(); }
    catch (DeepSleepSignal&) {}
    g_httpFailMs = 15000; g_httpCode = 200;
    const int elsoTeszt = serialIndex("Beginning Test.");
    const int reset     = serialIndex("Beginning Reset in FAILURE_STATE.");
    if (elsoTeszt < 0 || reset < 0) return -1;
    return uptimeSecAt((size_t)reset) - uptimeSecAt((size_t)elsoTeszt);
  };
  const int elo = merd(15000);
  printf("     [info] elo DNS, hallgato szerver: %d mp\n", elo);
  CHECK(elo >= 121 && elo <= 125, "elo DNS mellett 123 mp (a doksi szama)");
  const int halott = merd(33000);
  printf("     [info] halott DNS: %d mp\n", halott);
  CHECK(halott >= 211 && halott <= 215, "halott DNS mellett 213 mp");
}

// Hany teljes kort var ki az eszkoz, mielott feladja? A doksi 33 kort ir es
// ebbol 47 orat szamol - de a wifiGiveUp() ELOSZOR novel, AZTAN ellenoriz,
// tehat az UTOLSO kor mar nem alszik egyet. Ezt vegigjatsszuk.
static void scR8() {
  int alvasok = 0, korok = 0;
  bool apMod = false;
  for (int i = 0; i < 40 && !apMod; i++) {
    coldBoot(false, "MyNetwork", "titok123", "", "", 500, i > 0);
    korok++;
    bool slept = false;
    try { setup(); while (g_millis < 3u*60*60*1000 && deviceMode == (DeviceMode)0) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    if (slept) alvasok++;
    if (deviceMode == (DeviceMode)1) apMod = true;
  }
  printf("     [info] %d kor, ebbol %d alvassal; AP mod: %s\n",
         korok, alvasok, apMod ? "igen" : "nem");
  const double oraban = (korok * 25.5 + alvasok * 60.0) / 60.0;
  printf("     [info] osszes turelem: %d x 25,5 perc + %d x 60 perc = %.1f ora\n",
         korok, alvasok, oraban);
  CHECK(apMod, "vegul AP beallito modba ment");
  CHECK(korok == 33, "pontosan 33 kort probalt");
  CHECK(alvasok == 32, "de csak 32-szer aludt: az utolso kor mar nem alszik");
  CHECK(oraban > 45.5 && oraban < 46.5, "a tenyleges turelem ~46 ora, nem 47");
}

static void scR2() {
  // A 40. kör után feladja és AP módba megy (2 nap)
  coldBoot(false, "MyNetwork", "titok123", "", "");
  rtcRetryRounds = 32;                      // az utolsó kör előtt vagyunk
  setup();
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 20u*60*1000 && deviceMode == (DeviceMode)0) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "a 33. körnél már nem alszik el");
  CHECK(deviceMode == (DeviceMode)1, "AP beállító mód");
  CHECK(rtcRetryRounds == 0, "a számláló nullázva a következő ciklushoz");
}

static void scR3() {
  // A kör hossza tizedpercben, hogy a 90 mp-es pulzus is beleférjen
  const uint32_t roundTenths = 100   // 10,0 perc firstStartDelay
                             + 20    //  2,0 perc 3 próba
                             + 15    //  1,5 perc RESET_PULSE
                             + 100   // 10,0 perc RESET_DELAY
                             + 20    //  2,0 perc újabb 3 próba
                             + 600;  // 60,0 perc alvás
  CHECK(roundTenths == 855, "egy kör 85,5 perc");
  CHECK(33u * roundTenths == 28215u, "33 kör = 2821,5 perc");
  CHECK(28215u < 48u * 60u * 10u, "és ez kevesebb, mint 2 nap (28800 tizedperc)");
  CHECK(34u * roundTenths > 48u * 60u * 10u, "34 kör viszont már túllépné");
}

static void scR4() {
  // Sikeres csatlakozás nullázza a 2 napos ablakot
  coldBoot(true, "MyNetwork", "titok123", "", "");
  rtcRetryRounds = 17;
  setup();
  CHECK(deviceMode == (DeviceMode)0, "csatlakozott");
  CHECK(rtcRetryRounds == 0, "új 2 napos ablak indul");
}

static void scL1() {
  // Az eseménynapló rögzít és a /log oldal kiírja
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  setup();
  CHECK(rtcEvNext > 0, "rögzített eseményt");

  AsyncWebServerRequest req;
  g_handlers["/log#1"](&req);
  CHECK(req._code == 200, "a /log oldal kiszolgálva");
  CHECK(req._body.find("BOOT") != std::string::npos, "a BOOT esemény megjelenik");
  CHECK(req._body.find("Ujraprobalkozasi korok") != std::string::npos,
        "a kör számláló látszik");
  CHECK(req._body.find("Watchdog ujraindulasok") != std::string::npos,
        "a watchdog számláló látszik");
}

static void scL2() {
  // A napló túléli az újraindulást és a körpuffer nem csordul túl
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  setup();
  const uint32_t afterFirst = rtcEvNext;
  CHECK(afterFirst > 0, "első boot rögzítve");

  for (int i = 0; i < 50; i++) logEvent((EventCode)EV_TEST_FAIL_C, (uint16_t)i);
  CHECK(rtcEvNext == afterFirst + 50, "minden esemény számolva");

  AsyncWebServerRequest req;
  g_handlers["/log#1"](&req);
  CHECK(req._code == 200, "a /log továbbra is működik túlcsordulás után");
  // csak az utolsó 32 fér el
  CHECK(req._body.find(">TEST FAIL<") != std::string::npos, "a friss események láthatók");
  // Pontosan táblázatcellára illesztünk: a "BOOT" szó a magyarázó
  // szövegben is szerepel az oldalon.
  CHECK(req._body.find(">BOOT<") == std::string::npos,
        "a régi BOOT már kicsúszott a körpufferből");
}

static void scL3() {
  // A végzetes hiba oka is bekerül a naplóba
  coldBoot(true, "TestNet", "pw", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  g_fsMountOk = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  bool found = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == EV_FATAL_C && rtcEvents[i].param == 1) found = true;
  }
  CHECK(found, "a FATAL esemény a LittleFS okkal (param=1) rögzítve");
}


static void scSB1() {
  // Beragadt RESET gomb -> 60 mp alvas, gombebresztes nelkul
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[3] = LOW;                       // D1 = GPIO3
  bool slept = false; uint64_t us = 0;
  try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt");
  CHECK(us == 60ULL * 1000000ULL, "60 másodperces ébresztés");
  CHECK(g_gpioWakeMask == 0, "gomb-ébresztés NINCS armolva (boot loop ellen)");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scSB2() {
  // Beragadt WIFIRESET gomb -> ugyanaz a kezeles
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[2] = LOW;                       // D0 = GPIO2
  bool slept = false; uint64_t us = 0;
  try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "a wifireset gomb beragadását is elkapja");
  CHECK(us == 60ULL * 1000000ULL, "60 másodperces ébresztés");
  CHECK(g_gpioWakeMask == 0, "gomb-ébresztés NINCS armolva");
}

static void scSB3() {
  // A ket LED FELVALTVA villog (ellentetes fazis) - ez kulonbozteti meg a
  // vegzetes hibatol, ahol egyszerre villognak.
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[3] = LOW;
  g_log.clear();
  try { setup(); } catch (DeepSleepSignal&) {}

  // A firmware egymas utan irja a ket labat, ezert a naplo szomszedos parjait
  // nezzuk: a valtakozo mintat a "led BE + wifiled KI" es a forditottja adja.
  int alternating = 0, simultaneous = 0;
  for (size_t i = 0; i + 1 < g_log.size(); i++) {
    if (g_log[i] == "pin6=HIGH" && g_log[i+1] == "pin5=LOW") alternating++;
    else if (g_log[i] == "pin6=LOW" && g_log[i+1] == "pin5=HIGH") alternating++;
    else if (g_log[i] == "pin6=HIGH" && g_log[i+1] == "pin5=HIGH") simultaneous++;
    else if (g_log[i] == "pin6=LOW" && g_log[i+1] == "pin5=LOW") simultaneous++;
  }
  printf("     [info] valtakozo par: %d, egyutt-villogo par: %d\n",
         alternating, simultaneous);
  CHECK(alternating >= 10, "sokszor váltottak FELVÁLTVA (ellentétes fázis)");
  CHECK(simultaneous == 0, "egyszer sem villogtak EGYÜTT (az a végzetes hiba jele)");
  CHECK(g_pinState[6] == LOW && g_pinState[5] == LOW, "alvás előtt mindkettő lekapcsolva");
}

static void scSB4() {
  // A beragadt gomb bekerul a diagnosztikai naploba
  coldBoot(true, "TestNet", "pw", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  g_pinRead[2] = LOW;                       // wifireset
  try { setup(); } catch (DeepSleepSignal&) {}
  bool found = false, bootLogged = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 11 && rtcEvents[i].param == 1) found = true;
    if (rtcEvents[i].code == 1) bootLogged = true;
  }
  CHECK(bootLogged, "a BOOT esemény is bekerült (a gombellenőrzés előtt)");
  CHECK(found, "STUCK BUTTON esemény a wifireset gombbal (param=1)");
}


static void scWDT5() {
  // A setup() blokkoló ciklusai a watchdog bekapcsolása ELŐTT futnak.
  // Ott nem szabad etetni: a task még nincs feliratkozva, az
  // esp_task_wdt_reset() ESP_ERR_NOT_FOUND-ot ad, amire a core log_e()-t hív.
  // Egy sikertelen indulási csatlakozás 20 mp-e ~2000 hibasort jelentene.
  coldBoot(false, "MyNetwork", "pw", "", "");   // nem sikerül csatlakozni
  g_wdtFeedBeforeEnable = 0;
  setup();
  printf("     [info] etetes a feliratkozas elott: %u\n",
         (unsigned)g_wdtFeedBeforeEnable);
  CHECK(g_wdtEnabled, "a watchdog bekapcsolt a setup() végén");
  CHECK(g_wdtFeedBeforeEnable == 0,
        "a feliratkozás ELŐTT egyszer sem etettünk");
}


static void scSN1() {
  // Ha a gomb-ébresztés armolása HIBÁZIK (pl. valaki nem RTC-képes lábra tette
  // a gombot), az időzítő nélküli alvás örökre elérhetetlenné tenné az eszközt.
  // Ilyenkor biztonsági hálóként mégis armolunk egy hosszú időzítőt.
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;                  // -> MODE_FATAL
  g_gpioWakeResult = -1;                // az armolás elbukik
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  bool slept = false; uint64_t us = 1;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 8u*60*1000) loop(); }
  catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt");
  CHECK(g_gpioWakeMask == 0, "a gomb-ébresztés tényleg nem armolódott");
  CHECK(us == 3600ULL*1000000ULL,
        "helyette 1 órás időzítő - az eszköz nem válik elérhetetlenné");
}

static void scSN2() {
  // Ha az armolás sikerül, marad az eredeti viselkedés: NINCS időzítő
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  bool slept = false; uint64_t us = 1;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 8u*60*1000) loop(); }
  catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt");
  CHECK(g_gpioWakeMask == (1ULL << 3), "gomb-ébresztés armolva");
  CHECK(us == 0, "és NINCS időzített ébresztés");
}


// Hany soros sor keletkezett a megadott szimulalt ido alatt?
static uint32_t serialLinesPerMinute(uint32_t durMs) {
  const size_t before = g_serialLog.size();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < durMs && ++guard < 3000000) loop(); }
  catch (DeepSleepSignal&) {} catch (RestartSignal&) {}
  const uint32_t mins = (g_millis - t0) / 60000;
  return mins ? (uint32_t)((g_serialLog.size() - before) / mins) : 0;
}

static void scSER1() {
  // Normal mukodes: a soros kimenet ne arassza el a konzolt
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  loop();
  const uint32_t lpm = serialLinesPerMinute(30u * 60 * 1000);
  printf("     [info] %u sor/perc\n", lpm);
  CHECK(lpm <= 30, "normál működésben max ~30 sor/perc");
  CHECK(lpm > 0, "de azért ír valamit (nem néma)");
}

static void scSER2() {
  // Internet kiesett: a hibasorozat se generaljon aradatot
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Rossz"; pingSim.ok = false;
  setup();
  loop();
  const uint32_t lpm = serialLinesPerMinute(30u * 60 * 1000);
  printf("     [info] %u sor/perc\n", lpm);
  CHECK(lpm <= 30, "internet kiesésnél is max ~30 sor/perc");
}

static void scSER3() {
  // AP mod es hibajelzo mod: a villogo ciklus ne irjon semmit
  coldBoot(false, "", "", "", "");
  setup();
  const size_t before = g_serialLog.size();
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 4u*60*1000) loop(); } catch (DeepSleepSignal&) {}
  CHECK(g_serialLog.size() == before, "AP módban a loop() egy sort sem ír");

  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  const size_t before2 = g_serialLog.size();
  const uint32_t t1 = g_millis;
  try { while (g_millis - t1 < 4u*60*1000) loop(); } catch (DeepSleepSignal&) {}
  CHECK(g_serialLog.size() == before2, "hibajelző módban sem ír a villogó ciklus");
}

static void scOV1() {
  // A szamlalok nem csordulnak tul: az internet tartos kieseseben a
  // failedCount / cycleIndex korlatos marad, mert a router reset nullazza oket.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Rossz"; pingSim.ok = false;
  setup();
  loop();
  int guard = 0;
  uint32_t resets = 0;
  try {
    while (++guard < 400000) {
      const size_t before = g_log.size();
      loop(); g_millis += 10;
      for (size_t i = before; i < g_log.size(); i++)
        if (g_log[i] == RELAY_HIGH) resets++;
      if (resets >= 3) break;
    }
  } catch (DeepSleepSignal&) {}
  CHECK(resets >= 3, "több router reset ciklus lefutott");
  CHECK(guard < 400000, "nem ragadt be végtelen ciklusba");
}


// --- A watchdog feliratkozas tenyleges ellenorzese ---------------------------
// Az enableLoopWDT() (esp32-hal-misc.c) void: ha az esp_task_wdt_add() hibazik,
// csak egy log_e() jelzi. Ha vakon feltetelezzuk a sikert, ket baj tortenik:
// (1) azt hisszuk, vedve vagyunk, kozben egy lefagyas eszrevetlen marad,
// (2) minden feedLoopWDT() ESP_ERR_NOT_FOUND-ot kap -> 10 ms-onkent egy
//     log_e() sor a soros portra.
static void scWDT6() {
  // A TWDT nincs inicializalva (ESP_TASK_WDT_INIT=n). A sketchnek fel kell
  // huznia, es utana tenylegesen vedenie a loop()-ot.
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtInited = false; g_wdtEnabled = false;
  setup();
  CHECK(logIndex("wdt_init") >= 0, "a sketch inicializalta a TWDT-t");
  CHECK(g_wdtEnabled, "a loop task VEGUL fel van iratkozva");
  CHECK(g_wdtTimeoutMs == 90000 && g_wdtPanic, "90 mp-es timeout, panic bekapcsolva");
  CHECK(serialHas("Watchdog enabled"), "csak most jelenti sikeresnek");

  // A masik fele: feliratkozas nelkul minden etetes ESP_ERR_NOT_FOUND lenne,
  // amire a core log_e()-t hiv - 10 ms-onkent egy sor a soros portra.
  g_wdtFeedNotSubscribed = 0;
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 60u * 1000 && ++guard < 500000) loop(); }
  catch (...) {}
  CHECK(g_wdtFeedNotSubscribed == 0, "nincs feliratkozas nelkuli etetes (nincs log_e() aradat)");
}

static void scWDT7() {
  // A TWDT nincs inicializalva ES nem is huzhato fel. A feliratkozas tehat
  // sikertelen - ilyenkor TILOS etetni (az lenne az elarasztott konzol), es
  // tilos vedelmet allitani.
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtInited = false; g_wdtEnabled = false; g_wdtInitFails = true;
  setup();
  g_wdtFeedNotSubscribed = 0;
  const size_t before = g_serialLog.size();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 60u * 1000 && ++guard < 500000) loop(); }
  catch (...) {}
  CHECK(!g_wdtEnabled, "a loop task NINCS feliratkozva");
  CHECK(!serialHas("Watchdog enabled"), "nem allitja, hogy vedve van");
  CHECK(serialHas("FIGYELEM"), "kiirja, hogy a watchdog nem vedi a loop()-ot");
  CHECK(g_wdtFeedNotSubscribed == 0,
        "egyetlen feliratkozas nelkuli etetes sincs (nincs log_e() aradat)");
  CHECK(g_serialLog.size() - before < 60,
        "1 perc alatt sem arasztja el a konzolt");
}

static void scWDT8() {
  // A feliratkozas sikerul, de a konfiguralas nem. Feliratkozva maradni a be
  // nem allitott (tipikusan 5 mp-es) timeouttal maga lenne a csapda: a
  // http.GET() akar 33 mp-ig blokkol etetes nelkul, tehat MINDEN teszt
  // watchdog-panicba es ujrainditasi hurokba futna. Ilyenkor a helyes lepes
  // a leiratkozas: vedelem nincs, de a program legalabb fut.
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtReconfigureFails = true;
  setup();
  CHECK(!serialHas("Watchdog enabled"), "nem jelent sikeres 90 mp-es timeoutot");
  CHECK(serialHas("FIGYELEM"), "figyelmeztet, hogy nincs vedelem");
  CHECK(!g_wdtEnabled, "leiratkozott: az 5 mp-es default nem valhat csapdava");
  CHECK(logIndex("disableLoopWDT") > logIndex("enableLoopWDT"),
        "a leiratkozas a sikertelen konfiguralas KOVETKEZMENYE");
  // Etetes nelkuli mukodes: a feedWatchdog() kapuzva van, nincs log_e() aradat.
  g_wdtFeedNotSubscribed = 0;
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 30u * 1000 && ++guard < 300000) loop(); }
  catch (...) {}
  CHECK(g_wdtFeedNotSubscribed == 0,
        "leiratkozas utan egyetlen etetes sincs (nincs hibasor-aradat)");
}

// --- Diagnosztikai naplo: a dokumentalt esemenykodok tenyleg keletkeznek ----
static void scL4() {
  // Elso indulas mentett halozat nelkul -> AP mod. A naplobol ennek ki kell
  // derulnie, kulonben soros kabel nelkul nem tudni, miert all AP modban.
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP mod");
  bool found = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 6 && rtcEvents[i].param == 1) found = true;
  }
  CHECK(found, "AP MODE esemeny a 'nincs mentett SSID' okkal (param=1)");
}

static void scL5() {
  // Vegzetes hiba utani elalvas: ez az utolso dolog, ami tortent - a naplobol
  // meg kell latszania, hogy nem magatol halt meg az eszkoz.
  coldBoot(true, "TestNet", "pw", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  g_fsMountOk = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  bool slept = false;
  int guard = 0;
  try { while (++guard < 500000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "5 perc utan elaludt");
  CHECK(g_wakeupUs == 0, "idozitett ebresztes NINCS");
  bool found = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 8 && rtcEvents[i].param == 4) found = true;
  }
  CHECK(found, "SLEEP esemeny a vegzetes hiba okkal (param=4)");
}

// --- Felig kitoltott statikus IP --------------------------------------------
static void scP8() {
  // IP gateway nelkul: az initWiFi() ilyenkor DHCP-re esik vissza, tehat a
  // "menj a megadott fix cimre" uzenet hazugsag lenne.
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  int code = postConfig("Halozat", "jelszo123", "192.168.1.200", "", &body);
  CHECK(code == 500, "IP gateway nelkul -> 500, nem hamis siker");
  CHECK(body.find("gateway") != std::string::npos, "az indoklas megnevezi a gateway-t");
  CHECK(!restartPending, "nem indul ujra a hianyos konfiggal");

  code = postConfig("Halozat", "jelszo123", "", "192.168.1.1", &body);
  CHECK(code == 500, "gateway IP nelkul -> szinten 500");

  code = postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1", &body);
  CHECK(code == 200, "mindketto megadva -> 200");
  CHECK(restartPending, "es most mar ujraindul");
}

static void scP9() {
  // A csonkolatlan indoklas: a snprintf() puffere eleg nagy hozza.
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  postConfig("Halozat", "jelszo123", "192.168.1.200", "", &body);
  CHECK(body.find("probald meg ismet") != std::string::npos,
        "a hibauzenet vege sem csonkolodik le");
}

// --- Beragadt szerver: a sajat hatarido tartson ------------------------------
// ============ CHUNKED VALASZ ============
//
// A HTTPClient a darabhatarokat csak a getString() / writeToStream() utjan
// bontja le; mi egyiket sem hasznaljuk (korlatlanul foglalnanak), tehat a nyers
// streamben ott vannak a keretbajtok. Ezek nelkul a jol mukodo vegpont valasza
// is "Hamis ertek" lenne - vagyis egy atkeretezo proxy internetkimaradasnak
// latszana.

static void scCH1() {
  g_httpCode = 200; g_httpBeginOk = true;
  g_httpChunked = true; g_httpChunkSize = 0;   // egyetlen darab
  g_httpBody = "Microsoft NCSI";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "chunked valasz is egyezik");
  CHECK(!testInternetHTTP("http://x/", "Valami mas"), "de a tartalmat tovabbra is nezi");
}

static void scCH2() {
  g_httpCode = 200; g_httpBeginOk = true;
  g_httpChunked = true; g_httpChunkSize = 3;   // 5 darabra vagva
  g_httpBody = "Microsoft NCSI";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "tobb darabot is osszefuz");
  g_httpChunkSize = 1;
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "bajtonkent darabolva is");
}

static void scCH3() {
  // Nagybetus hexa es darab-kiterjesztes: az RFC 9112 mindkettot engedi.
  g_httpCode = 200; g_httpBeginOk = true; g_httpChunked = true;
  g_httpRawOverride = "E;padding=xyz\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "kiterjesztest es nagybetus hexat is kezel");
  g_httpRawOverride = "e\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "kisbetus hexa is jo");
  g_httpRawOverride.clear();
}

static void scCH4() {
  // Szabalytalan keretezes: nem talalgatunk, a teszt elbukik - ez a biztonsagos
  // irany (egy captive portal soha ne szamitson sikeres internettesztnek).
  g_httpCode = 200; g_httpBeginOk = true; g_httpChunked = true;
  const uint32_t t0 = g_millis;
  g_httpRawOverride = "zz\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "hexa helyett szemet -> nem egyezik");
  // A meret sor 5 bajtot iger, de a kapcsolat 3 utan veget er.
  g_httpRawOverride = "5\r\nMic";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "csonka darab -> nem egyezik");
  // Tulcsordulasra hajto meret: a 32 bites szamlalo korbefordulna, es egy
  // "kicsi" meretet kapnank egy oriasi darabra.
  g_httpRawOverride = "FFFFFFFFFFFF\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "tulcsordulo darabmeret -> nem egyezik");
  CHECK(g_millis - t0 < 5000, "es egyik esetben sem var ki timeoutot");
  g_httpRawOverride.clear();
}

static void scCH5() {
  // Egy captive portal darabokban is kuldhet szazezer bajtot. A getSize() ilyenkor
  // -1, tehat a meret-alapu korai elutasitas nem vedi meg: a darabolvasonak kell
  // megallnia a puffer hataran.
  g_httpCode = 200; g_httpBeginOk = true; g_httpChunked = true;
  g_httpChunkSize = 100;
  g_httpBody = std::string(50000, 'x');
  const uint32_t t0 = g_millis;
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "nem egyezik, es nem is szallt el");
  CHECK(g_millis - t0 < 5000, "nem olvassa vegig az 50 kB-ot");
  g_httpChunkSize = 0; g_httpBody = "Microsoft NCSI";
}

static void scH5() {
  // A kapcsolat el, de nem jon tobb adat. A readBounded() nem varhat a socket
  // sajat fogadasi timeoutjara, mert az nem a mienk - a sajat hataridejevel
  // kell kilepnie, jocskan a watchdog timeout alatt.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpCode = 200; g_httpSize = -1; g_httpBody = "Micro";  // fel valasz
  g_httpStall = true;
  const uint32_t t0 = g_millis;
  const bool ok = testInternetHTTP("http://pelda/x", "Microsoft NCSI");
  const uint32_t elapsed = g_millis - t0;
  g_httpStall = false;
  CHECK(!ok, "a csonka valasz nem egyezik");
  CHECK(elapsed < 5000, "a hatarido ~1,5 mp korul tart, nem 10 mp-ig var");
  CHECK(elapsed < 90000, "biztosan a watchdog timeout alatt marad");
}

// --- "generate_204" stilusu vegpont -----------------------------------------
// Ures elvart valasz eseten nem szoveget egyeztetunk, hanem a 204 No Content
// statuszkodot varjuk. Ez szigorubb: egy captive portal nem tud 204-et adni,
// mert neki eppenseggel tartalmat kell kuldenie (bejelentkezo oldal vagy
// atiranyitas). Ugyanezt a dontest hozza a NetworkManager is.
static void scH6() {
  g_httpBeginOk = true; g_httpSize = -2;
  g_httpCode = 204; g_httpBody = "";
  CHECK(testInternetHTTP("http://x/generate_204", ""), "204 + üres elvárás -> siker");

  g_httpCode = 200; g_httpBody = "";
  CHECK(!testInternetHTTP("http://x/generate_204", ""),
        "üres törzsű 200 nem elég, ha 204-et várunk");

  g_httpCode = 200; g_httpBody = "<html>Jelentkezzen be a WiFi hasznalatahoz</html>";
  CHECK(!testInternetHTTP("http://x/generate_204", ""),
        "captive portal 200 + HTML -> elbukik");

  g_httpCode = 302; g_httpBody = "";
  CHECK(!testInternetHTTP("http://x/generate_204", ""), "átirányítás -> elbukik");

  g_httpCode = -1; g_httpBody = "";
  CHECK(!testInternetHTTP("http://x/generate_204", ""), "kapcsolódási hiba -> elbukik");

  // A szoveges ag nem lazul fel: ott tovabbra is 200 kell.
  g_httpCode = 204; g_httpBody = "Microsoft Connect Test";
  CHECK(!testInternetHTTP("http://x/", "Microsoft Connect Test"),
        "a szöveges teszt nem fogad el 204-et");
  g_httpCode = 200; g_httpSize = -2;
}

static void scH7() {
  // 204-nel a torzs olvasasaba bele sem szabad kezdeni: ures streamnel a
  // readBounded() a sajat 1,5 mp-es hataridejeig varna a semmire.
  g_httpBeginOk = true; g_httpCode = 204; g_httpBody = ""; g_httpSize = -1;
  const uint32_t t0 = g_millis;
  CHECK(testInternetHTTP("http://x/generate_204", ""), "204 -> siker");
  CHECK(g_millis - t0 < 1500,  // HTTP_READ_TIMEOUT_MS
        "a törzset el sem kezdi olvasni, nincs 1,5 mp várakozás");
  g_httpCode = 200; g_httpSize = -2;
}

// --- Az eszkalacio tenyleg kulonbozo vegpontokat probal ----------------------
// Regresszio arra, hogy egyetlen uzemelteto kiesese ne dontson a router
// ujrainditasarol: az 5 teszt 5 kulonbozo cel, 5 kulonbozo uzemelteto.
// ICMP nincs kozottuk - lasd scH9.
static void scH8() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  g_httpUrls.clear(); pingSim.targets.clear();

  int guard = 0; bool relay = false;
  try {
    while (++guard < 400000 && !relay) {
      const size_t before = g_log.size();
      loop();
      for (size_t i = before; i < g_log.size(); i++)
        if (g_log[i] == RELAY_HIGH) relay = true;
    }
  } catch (DeepSleepSignal&) {}

  CHECK(relay, "az eszkaláció végén elindul a router reset");
  CHECK(g_httpUrls.size() == 5, "mind az 5 teszt HTTP volt");
  if (g_httpUrls.size() == 5) {
    CHECK(g_httpUrls[0] == "http://www.msftconnecttest.com/connecttest.txt",
          "0: Microsoft");
    CHECK(g_httpUrls[1] == "http://cp.cloudflare.com/generate_204",
          "1: Cloudflare");
    CHECK(g_httpUrls[2] == "http://detectportal.firefox.com/success.txt",
          "2: Mozilla");
    CHECK(g_httpUrls[3] == "http://nmcheck.gnome.org/check_network_status.txt",
          "3: GNOME / NetworkManager");
    CHECK(g_httpUrls[4] == "http://connectivitycheck.gstatic.com/generate_204",
          "4: Google");
    std::set<std::string> uniq(g_httpUrls.begin(), g_httpUrls.end());
    CHECK(uniq.size() == 5, "öt különböző végpont, egyik sem ismétlődik");
  }
  // A ping MOSTANTOL letezik a programban - de csak a hosszu varakozasok korai
  // lezarasahoz (onlineProbe), sosem az internetteszt reszekent. A szerzodes
  // tehat nem az, hogy "nincs ping", hanem hogy egyetlen ping sem HELYETTESIT
  // egy HTTP tesztet: az eszkalacio mind az ot vegpontot vegigprobalta.
  bool csakProbeCel = true;
  for (auto& t : pingSim.targets) {
    if (t != "1.1.1.1") csakProbeCel = false;
  }
  CHECK(csakProbeCel,
        "minden ping a proba celpontjara ment, egyik sem az internetteszt resze");
}

// --- Befagyott router-DNS: HTTP semmi, ICMP tokeletes -----------------------
// Ez a leggyakoribb "csatlakozva, de nincs internet" hiba olcso routereken
// (a dnsmasq beragad). Amig a ping is szavazhatott, az eszkoz orakon at
// tetlen maradt: 41 bukott HTTP teszt mellett 0 router reset. Mostantol
// mind az ot teszt nevfeloldast igenyel, tehat ezt eszreveszi.
static void scH9() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1;      // egyetlen nev sem oldodik fel
  pingSim.ok = true;    // de az IP szintu utvonal hibatlan
  setup();

  int guard = 0, relays = 0;
  const uint32_t t0 = g_millis;
  size_t before = g_log.size();
  try {
    while (++guard < 400000 && g_millis - t0 < 30u * 60 * 1000 && relays == 0) {
      loop();
      for (size_t i = before; i < g_log.size(); i++)
        if (g_log[i] == RELAY_HIGH) relays++;
      before = g_log.size();
    }
  } catch (DeepSleepSignal&) {}
  CHECK(relays == 1, "a befagyott DNS-t router resettel kezeli");
  CHECK(g_millis - t0 < 5u * 60 * 1000, "5 percen belül, nem órákon át tétlenül");
  // A regi teszt azt kotötte ki, hogy egyaltalan NINCS ping. Az onlineProbe()
  // ota van - es epp ezert lett ez a teszt ERŐSEBB: most azt allitjuk, hogy a
  // ping tenyleg lefutott ES SIKERULT (pingSim.ok = true), a router reset
  // megis megtortent. Vagyis a ping semmilyen uton nem tud "atszavazni" a
  // HTTP tesztek felett - ez a H9 eredeti lenyege.
  CHECK(!pingSim.targets.empty(), "a proba tenyleg pingelt (es sikerult is)");
  CHECK(g_httpUrls.size() >= 5, "es mind az ot HTTP teszt lefutott");
}


// --- Csak IPv4 hasznalhato --------------------------------------------------
// Az IPAddress::fromString() az IPv4 utan IPv6-ot is megprobal, ezert a "::1"
// is ervenyesnek latszik - de a WiFi.config() az uint32_t konverziot hasznalja,
// ami IPv6-ra 0-t ad. Igy egy IPv6 cim csendben DHCP-t, vegyes paros eseten
// pedig gateway es DNS nelkuli statikus IP-t eredmenyezne.
static void scIP1() {
  // Eloszor: a stub tenyleg ugy viselkedik, mint a core.
  IPAddress a;
  CHECK(a.fromString("::1"), "a core-hoz huen a '::1' ervenyes cimnek szamit");
  CHECK((uint32_t)a == 0, "de az uint32_t konverzio 0-t ad (IPAddress.h:83)");
  IPAddress b;
  CHECK(b.fromString("192.168.1.5") && (uint32_t)b != 0, "IPv4 viszont nem nulla");
}

static void scIP2() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  int code = postConfig("Halozat", "jelszo123", "::1", "fe80::1", &body);
  CHECK(code == 500, "IPv6 cimparos -> 500, nem csendes DHCP");
  CHECK(!restartPending, "nem indul ujra ervenytelen cimmel");

  // A vegyes paros a rosszabb eset: IPv4 IP + IPv6 gateway eseten a config()
  // 0.0.0.0-s gateway-t ES 0.0.0.0-s elsodleges DNS-t allitana be.
  code = postConfig("Halozat", "jelszo123", "192.168.1.200", "fe80::1", &body);
  CHECK(code == 500, "IPv4 IP + IPv6 gateway -> 500");

  code = postConfig("Halozat", "jelszo123", "0.0.0.0", "192.168.1.1", &body);
  CHECK(code == 500, "0.0.0.0 sem fogadhato el (a config() DHCP-nek venne)");

  code = postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1", &body);
  CHECK(code == 200, "ervenyes IPv4 paros -> 200");
}

static void scIP3() {
  // Regebbi firmware IPv6 cimet is elmenthetett: az initWiFi()-nek akkor is
  // DHCP-re kell esnie, nem gateway nelkuli statikus IP-t beallitania.
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "fe80::1");
  setup();
  CHECK(wifiSim.configCount == 0, "nem hivott config()-ot a hibas parossal");
  CHECK(!wifiSim.staticApplied, "DHCP-re esett vissza");
  CHECK(serialHas("Invalid gateway format"), "es meg is mondja, miert");
  CHECK(wifiSim.beginCount == 1, "de csatlakozni azert megprobalt");
}

// --- Fajliras kozben nincs ujrainditas --------------------------------------
static void scP10() {
  // A felhasznalo eppen menteskor nyomja meg a reset gombot. A mentes kozbeni
  // ujrainditas felig kiirt konfiguraciot hagyna - ugyanaz a szabaly, mint az
  // elalvasnal.
  coldBoot(false, "", "", "", "");
  setup();
  savingConfig = true;
  g_pinRead[3] = LOW;                      // D1 = GPIO3, reset gomb
  bool restarted = false;
  try {
    for (int i = 0; i < 50; i++) { resetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "mentes kozben NEM indul ujra");

  savingConfig = false;
  try {
    // A mentes alatt a debounce sem indult el, tehat itt egy teljes 50 ms-os
    // lenyomas kell - nehany tized masodperc bosegesen eleg ra.
    for (int i = 0; i < 20; i++) { resetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a mentes utan viszont lefut (uj 50 ms debounce utan)");
}

static void scP11() {
  // Ugyanez a wifireset gombra: az mentes kozben a fajlokat is torolne, mikozben
  // az aszinkron task eppen irja oket.
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/ssid.txt"] = "RegiHalozat";
  savingConfig = true;
  g_pinRead[2] = LOW;                      // D0 = GPIO2, wifireset gomb
  bool restarted = false;
  try {
    for (int i = 0; i < 50; i++) { wifiresetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "mentes kozben NEM indul ujra");
  CHECK(g_fs["/ssid.txt"] == "RegiHalozat", "es nem is torolte a fajlokat");

  savingConfig = false;
  try {
    for (int i = 0; i < 20; i++) { wifiresetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a mentes utan viszont torol es ujraindit");
}


// --- Amit beirsz, azt is hasznalja -----------------------------------------
static void scP12() {
  // A readConfigValue() beolvasaskor levagja a whitespace-t. Ha mentéskor nem
  // vagnank, a fajlban mas lenne, mint amivel az eszkoz csatlakozik - es a
  // portal is a nyers erteket visszhangozna.
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig("  MyNetwork  ", "  titok123  ", "", "");
  CHECK(code == 200, "elfogadja");
  CHECK(g_fs["/ssid.txt"] == "MyNetwork", "az elmentett SSID mar vagott");
  CHECK(storedPass() == "titok123", "a jelszo is");
  CHECK(serialHas("SSID set to: MyNetwork"), "es a visszajelzes is a vagott ertek");
}

static void scP13() {
  // Csupa szokozbol allo SSID: a vagas utan ures marad. Ilyet nem szabad
  // sikerkent elfogadni - ujraindulas utan ugyanitt, AP modban kotnenk ki.
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("     ", "jelszo123", "", "", &body);
  CHECK(code == 500, "csupa szokoz SSID -> 500");
  CHECK(!restartPending, "nem indul ujra hasznalhatatlan SSID-vel");
}


// --- A LED-ek ne hazudjanak -------------------------------------------------
static void scLED1() {
  // A reset pulzus alatt a STATUSZ LED VILLOG (2 Hz), a Wi-Fi LED viszont
  // vegig sotet: a router ilyenkor aram nelkul van, tehat kapcsolat sincs.
  //
  // A korabbi valtozat mindket LED-et sotetre kapcsolta, es ez a teszt azt
  // rogzitette. 90 masodpercnyi sotet LED viszont ranezesre a HALOTT
  // eszkoztol sem kulonboztetheto meg - ezert villog most a statusz LED.
  // FIGYELEM a teszt erejere: a regi "volt-e pin6=LOW" allitas a villogasra
  // is IGAZ lenne, tehat csendben tovabb ment volna. Ezert nezzuk MINDKET
  // irany valtasait, es szamoljuk is oket.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_pinState[PIN_WIFILED] == HIGH, "csatlakozas utan vilagit a Wi-Fi LED (GPIO5)");

  int guard = 0;
  while (!reset_device() && ++guard < 200000) { feedLoopWDT(); delay(10); }
  CHECK(guard < 200000, "a reset pulzus lefutott");

  const int relayOn = logIndex(RELAY_HIGH);
  CHECK(relayOn >= 0, "a rele bekapcsolt");
  bool wifiLedOnInPulse = false;
  int statusLow = 0, statusHigh = 0;
  for (size_t i = (size_t)relayOn; i < g_log.size(); i++) {
    if (g_log[i] == RELAY_LOW && (int)i > relayOn) break;
    if (g_log[i] == "pin5=HIGH") wifiLedOnInPulse = true;
    if (g_log[i] == LED_LOW)  statusLow++;
    if (g_log[i] == LED_HIGH) statusHigh++;
  }
  CHECK(statusLow > 100 && statusHigh > 100,
        "a statusz LED VILLOG a pulzus alatt (90 mp / 2 Hz -> tobb szaz valtas)");
  CHECK(!wifiLedOnInPulse, "a Wi-Fi LED egyszer sem gyullad ki - nincs halozat");
  CHECK(g_pinState[PIN_LED] == HIGH, "a pulzus utan folyamatosra all vissza");
}


// ===========================================================================
// A kovetkezo esetek a lefedettseg-meres (gcov) alapjan keszultek: ezeket az
// agakat a 111 forgatokonyv egyike sem futtatta le, tehat semmi nem vedte oket.
// ===========================================================================

// --- A halasztott ujrainditas tenyleg lefut ---------------------------------
static void scP14() {
  // A sikeres mentes utan a loop() feladata ujrainditani, miutan a valasz
  // kiment. Eddig csak azt ellenoriztuk, hogy a jelzo beall - azt nem, hogy
  // az ujrainditas valoban megtortenik, es hogy a turelmi ido elotte NEM.
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "", "") == 200, "mentes OK");
  CHECK(restartPending, "ujrainditas beutemezve");

  bool early = false;
  try {
    // A turelmi ido (2 mp) alatt meg nem szabad ujraindulnia: a valasznak
    // ki kell mennie a bongeszo fele.
    for (int i = 0; i < 150; i++) { loop(); }     // ~1,5 mp
  } catch (RestartSignal&) { early = true; }
  CHECK(!early, "a 2 mp-es turelmi ido alatt NEM indul ujra");

  bool restarted = false;
  try {
    for (int i = 0; i < 200; i++) { loop(); }     // tovabbi ~2 mp
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a turelmi ido utan viszont ujraindul");
}

// --- A naplo cimkei ---------------------------------------------------------
static void scL6() {
  // A /log oldal emberi olvasasra szant resze. Ha egy cimke elcsuszna, a
  // naplo felrevezetne - es eddig csak a BOOT sor volt tesztelve.
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  setup();
  logEvent((EventCode)2, 7);    // WIFI OK
  logEvent((EventCode)3, 3);    // WIFI LOST
  logEvent((EventCode)4, 1);    // TEST FAIL
  logEvent((EventCode)5, 2);    // ROUTER RESET
  logEvent((EventCode)7, 0);    // CONFIG SAVED
  logEvent((EventCode)8, 4);    // SLEEP
  logEvent((EventCode)9, 3);    // FATAL
  logEvent((EventCode)10, 2);   // WDT RESET
  logEvent((EventCode)11, 1);   // STUCK BUTTON
  logEvent((EventCode)12, 1);   // GW UNREACH
  // A 6-os (AP MODE) sort a coldBoot(SSID nelkul) + setup() mar beirta.
  // A 12-es viszont eddig KIMARADT: a test/README azt allitotta, hogy ez a
  // teszt MINDEN kodot lefed, a lefedettseg-meres pedig azt mutatta, hogy a
  // "GW UNREACH" cimke sora sosem fut le. Az allitas es a teszt szetcsuszott.

  AsyncWebServerRequest req;
  g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  const char* want[] = { ">BOOT<", ">WIFI OK<", ">WIFI LOST<", ">TEST FAIL<",
                         ">ROUTER RESET<", ">AP MODE<", ">CONFIG SAVED<",
                         ">SLEEP<", ">FATAL<", ">WDT RESET<", ">STUCK BUTTON<",
                         ">GW UNREACH<" };
  bool all = true;
  for (const char* w : want) if (b.find(w) == std::string::npos) { all = false; printf("     [info] hianyzik: %s\n", w); }
  CHECK(all, "MIND a 12 esemenykod olvashato cimket kap");
  CHECK(b.find(">?<") == std::string::npos, "nincs ismeretlen kod a naplóban");
}

static void scL7() {
  // Ures naplo: a /log oldal ne dobjon tablat fejlec nelkul.
  coldBoot(false, "", "", "", "");
  setup();
  rtcEvMagic = 0; rtcEvNext = 0;      // a setup() BOOT sora utan uritjuk
  AsyncWebServerRequest req;
  g_handlers["/log#1"](&req);
  CHECK(req._body.find("Nincs rogzitett esemeny") != std::string::npos,
        "ures naplonal ezt irja ki");
  CHECK(req._body.find("<table") == std::string::npos, "es nem rajzol ures tablat");
}

// --- A tobbi HTTP vegpont ---------------------------------------------------
static void scF2() {
  // A portal a LittleFS-rol SEMMIT nem szolgal ki. Ez a legfontosabb
  // biztonsagi tulajdonsaga: a /pass.txt ugyanabban a gyokerben van, mint
  // barmi mas, amit valaha kiszolgalnank. A teszt ezert szandekosan olyan
  // fajlokat tesz fel, amiket a regi valtozat MEG kiadott volna.
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/wifimanager.html"] = "<html>REGI FAJL</html>";
  g_fs["/style.css"] = "body{}";
  g_fs["/favicon.png"] = "png";

  AsyncWebServerRequest a; g_handlers["/#1"](&a);
  CHECK(a._code == 200, "/ -> 200");
  CHECK(a._body.find("REGI FAJL") == std::string::npos,
        "a / a BEEPITETT urlapot adja, nem a fajlrendszeren talalt HTML-t");
  CHECK(a._body.find("name=\"ssid\"") != std::string::npos,
        "es ez tenyleg a beallito urlap");

  // Egyetlen olyan utvonal sincs regisztralva, ami a LittleFS-bol olvasna.
  int fsUtvonalak = 0;
  for (auto& kv : g_handlers) {
    if (kv.first.rfind("/style.css", 0) == 0) fsUtvonalak++;
    if (kv.first.rfind("/favicon", 0) == 0)   fsUtvonalak++;
    if (kv.first.rfind("/wifimanager", 0) == 0) fsUtvonalak++;
  }
  CHECK(fsUtvonalak == 0, "nincs egyetlen fajlkiszolgalo utvonal sem");
}

static void scF3() {
  // Ismeretlen utvonal: 404, es semmikepp nem szivarog ki fajltartalom.
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/pass.txt"] = "szupertitkos";
  AsyncWebServerRequest req;
  g_handlers["404"](&req);
  CHECK(req._code == 404, "ismeretlen utvonal -> 404");
  CHECK(req._body.find("szupertitkos") == std::string::npos, "nem szivarog jelszo");
}

static void scF4() {
  // Minden vegpont kitolja az AP hataridot - kulonben olvasgatas kozben
  // elaludna az eszkoz.
  coldBoot(false, "", "", "", "");
  setup();
  const uint32_t first = apDeadline;
  g_millis += 60u * 1000;
  AsyncWebServerRequest a; g_handlers["/ping#1"](&a);
  CHECK(apDeadline > first, "a /ping is kitolja a hataridot");
  const uint32_t second = apDeadline;
  g_millis += 60u * 1000;
  AsyncWebServerRequest b; g_handlers["/log#1"](&b);
  CHECK(apDeadline > second, "a /log is kitolja");

  // A doksi "minden HTTP kerest" iger. A 404 is interakcio: a bongeszo
  // magatol keri a /favicon.ico-t, es egy elgepelt cim sem jelenti azt,
  // hogy a felhasznalo elment.
  const uint32_t third = apDeadline;
  g_millis += 60u * 1000;
  AsyncWebServerRequest c; g_handlers["404"](&c);
  CHECK(c._code == 404, "ismeretlen utvonal tovabbra is 404");
  CHECK(apDeadline > third, "es a 404 is kitolja a hataridot");

  // A kitolas ABSZOLUT: mindig uj 5 perc, nem kumulativ.
  CHECK(apDeadline == g_millis + 5u * 60 * 1000,
        "a hatarido pontosan 5 perccel a keres utanra kerul");
}

// --- Tovabbi validacio ------------------------------------------------------
static void scP15() {
  // A HTML maxlength csak a bongeszot koti; egy sajat POST barmit kuldhet.
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  std::string longPass(80, 'x');
  const int code = postConfig("Halozat", longPass.c_str(), "", "", &body);
  CHECK(code == 500, "tul hosszu jelszo -> 500");
  CHECK(body.find("jelszo") != std::string::npos, "az indoklas a jelszot nevezi meg");
  CHECK(!restartPending, "nem indul ujra");
}

// --- Monitorozas: a kapcsolat visszajon -------------------------------------
static void scE6() {
  // A kapcsolat kiesik, majd a reconnectWifi() visszahozza. Ilyenkor NEM szabad
  // routert ujrainditani - eddig ez az ag teljesen tesztelet len volt.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpBody = "Microsoft Connect Test";
  loop();                                   // firstStart lezarasa
  rtcRetryRounds = 3;                       // legyen mit nullazni
  wifiSim.begun = false;                    // a kapcsolat kiesett
  wifiSim.willConnect = true;               // de azonnal visszajon
  g_log.clear();
  int guard = 0;
  try { while (++guard < 100000 && logIndex(RELAY_HIGH) < 0
               && !serialHas("Beginning Test.")) loop(); }
  catch (...) {}
  CHECK(logIndex(RELAY_HIGH) < 0, "NEM indit routert, ha visszajott a WiFi");
  CHECK(serialHas("WIFI RECONNECTED!"), "ujracsatlakozott");
  CHECK(rtcRetryRounds == 0, "a sikeres teszt nullazza a 2 napos ablakot");
}


// --- A wifireset a legfontosabbat torli eloszor -----------------------------
static void scX7() {
  // A "wifireset" celja, hogy az eszkoz a beallito portalon jojjon fel, es ezt
  // egyedul a /ssid.txt donti el. Ha a torles kozben elmegy az aram, akkor is
  // a kivant vegallapotban kell maradni - tehat az SSID megy eloszor.
  coldBoot(true, "TestNet", "pw", "192.168.1.5", "192.168.1.1");
  setup();
  g_log.clear(); g_serialLog.clear();       // a setup() sorai ne zavarjanak
  g_pinRead[2] = LOW;                       // D0 = GPIO2, wifireset
  try { for (int i = 0; i < 20; i++) { wifiresetbutton(); delay(10); } }
  catch (RestartSignal&) {}
  // A clearConfigValue() minden fajlt bejelent ("Clearing file: ..."), tehat
  // a soros naplo sorrendje mutatja a torles sorrendjet.
  int iSsid = serialIndex("Clearing file: /ssid.txt");
  int iPass = serialIndex("Clearing file: /pass.txt");
  int iIp   = serialIndex("Clearing file: /ip.txt");
  int iGw   = serialIndex("Clearing file: /gateway.txt");
  CHECK(iSsid >= 0 && iPass >= 0 && iIp >= 0 && iGw >= 0, "mind a negy fajlt erinti");
  CHECK(iSsid < iPass && iSsid < iIp && iSsid < iGw, "az SSID torlese az ELSO");
}

static void scX8() {
  // Ha a fajlrendszer nem irhato, az eszkoz NEM mukodhet tovabb: a konfiguracio
  // mentese ugyanigy elbukna, az ujrainditas pedig a regi adatokkal jonne fel.
  // Ugyanaz a hibaosztaly, mint a tobbi LittleFS hiba -> ugyanaz a kezeles.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  rtcEvMagic = 0; rtcEvNext = 0;
  g_fsWritable = false; g_fsRemoveOk = false;   // se irni, se torolni nem tud
  g_pinRead[2] = LOW;
  g_log.clear();
  bool restarted = false, slept = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 200000; i++) { wifiresetbutton(); delay(10); } }
  catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) { slept = true; }

  CHECK(!restarted, "NEM indul ujra a regi adatokkal");
  CHECK(slept, "hanem vegzetes hibat jelez, majd elalszik");
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  CHECK(serialHas("VEGZETES HIBA"), "a soros porton is vegzetes hibat jelent");
  CHECK(g_wakeupUs == 0, "idozitett ebresztes NINCS - csak gomb vagy aramtalanitas");

  const uint32_t elapsed = g_millis - t0;
  CHECK(elapsed >= 5u * 60 * 1000 && elapsed < 6u * 60 * 1000,
        "5 percig jelez, csak utana alszik el");

  bool logged = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++)
    if (rtcEvents[i].code == EV_FATAL_C && rtcEvents[i].param == 4) logged = true;
  CHECK(logged, "a naploba is bekerul (FATAL, param=4)");
}

static void scX12() {
  // A jelzes ugyanaz, mint a tobbi vegzetes hibanal: a ket LED EGYUTT villog
  // (a beragadt gombnal felvaltva) - igy ranezesre megkulonboztetheto.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fsWritable = false; g_fsRemoveOk = false;
  g_pinRead[2] = LOW;
  g_log.clear();
  try { for (int i = 0; i < 200000; i++) { wifiresetbutton(); delay(10); } }
  catch (...) {}
  int together = 0, opposite = 0;
  for (size_t i = 0; i + 1 < g_log.size(); i++) {
    // A ket digitalWrite egymas utan jon; a parokat nezzuk.
    if (g_log[i] == "pin6=HIGH" && g_log[i+1] == "pin5=HIGH") together++;
    if (g_log[i] == "pin6=LOW"  && g_log[i+1] == "pin5=LOW")  together++;
    if (g_log[i] == "pin6=HIGH" && g_log[i+1] == "pin5=LOW")  opposite++;
    if (g_log[i] == "pin6=LOW"  && g_log[i+1] == "pin5=HIGH") opposite++;
  }
  CHECK(together > 100, "a ket LED EGYUTT villog (vegzetes hiba jelzese)");
  CHECK(opposite == 0, "sosem ellentetes fazisban (az a beragadt gomb jele)");
}

static void scX13() {
  // A vegzetes jelzes alatt a reset gomb az egyetlen kiut - annak viszont
  // mukodnie kell, kulonben csak aramtalanitassal lehetne kimaszni.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fsWritable = false; g_fsRemoveOk = false;
  g_pinRead[2] = LOW;
  // A reset gombot is nyomva tartjuk. A fatalHalt() blokkol es nem ter vissza,
  // ezert a teszt ciklusabol menet kozben nem lehetne "megnyomni" - a
  // felhasznalo viszont barmikor lenyomhatja, tehat eleve nyomva modellezzuk.
  g_pinRead[3] = LOW;
  const uint32_t t0 = g_millis;
  bool restarted = false, slept = false;
  try { for (int i = 0; i < 200000; i++) { wifiresetbutton(); delay(10); } }
  catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(restarted, "a reset gomb a hibajelzes alatt is ujraindit");
  CHECK(!slept, "tehat nem varja ki az 5 percet");
  CHECK(g_millis - t0 < 5u * 60 * 1000, "meg az 5 perces hatarido elott");
}

static void scX14() {
  // Regresszio: a SIKERES torles tovabbra is egyszeru ujrainditas legyen.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_pinRead[2] = LOW;
  bool restarted = false, slept = false;
  try { for (int i = 0; i < 20; i++) { wifiresetbutton(); delay(10); } }
  catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(restarted && !slept, "sikeres torles -> ujrainditas, nem hibajelzes");
  CHECK(deviceMode != (DeviceMode)2, "nem megy MODE_FATAL-ba");
}

// --- A query-string parameter nem konfiguralhat -----------------------------
static void scX9() {
  // A request->params() a GET query parametereket IS visszaadja. Ha ezeket nem
  // szurnenk ki, egy sima link (/?ssid=...) atirhatna a konfiguraciot.
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/ssid.txt"] = "EredetiHalozat";
  AsyncWebServerRequest req;
  req.addParam("ssid", "TamadoHalozat", false);   // isPost() == false
  req.addParam("pass", "tamado", false);
  g_handlers["/#2"](&req);
  CHECK(g_fs["/ssid.txt"] == "EredetiHalozat", "a query parameter NEM irja at az SSID-t");
  CHECK(req._code == 500, "SSID nelkuli mentes -> 500");
  CHECK(!restartPending, "es nem indit ujra");
}

// --- Egy napnal hosszabb uptime ---------------------------------------------
static void scX10() {
  // Az uptime kiirasnak kulon aga van 1 napon tul. Egy 24/7-ben futo eszkoznel
  // ez az ag lesz a normalis - eddig egyetlen teszt sem jart benne.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const size_t before = g_serialLog.size();
  g_millis = 3u * 86400u * 1000u + 5u * 3600u * 1000u + 7u * 60u * 1000u + 9u * 1000u;
  printUptime();
  bool found = false;
  for (size_t i = before; i < g_serialLog.size(); i++)
    if (g_serialLog[i].find("Uptime: 3d 5h 7m 9s") != std::string::npos) found = true;
  CHECK(found, "3 nap 5 ora 7 perc 9 mp helyesen jelenik meg");
}

// --- Sikertelen statikus IP konfiguralas ------------------------------------
static void scX11() {
  // Ha a WiFi.config() elbukik, akkor sem szabad elvernie a csatlakozast:
  // DHCP-vel meg mindig mukodhet az eszkoz.
  coldBoot(true, "TestNet", "pw", "192.168.1.5", "192.168.1.1");
  wifiSim.configFails = true;
  setup();
  CHECK(serialHas("STA Failed to configure"), "jelzi a hibat");
  CHECK(wifiSim.beginCount >= 1, "de azert megprobal csatlakozni");
  CHECK(deviceMode == (DeviceMode)0, "es monitor modban marad");
}


// --- A visszaolvasasos ellenorzes tenyleg ved -------------------------------
static void scFS7() {
  // A legalattomosabb hiba: a print() a helyes bajtszamot adja vissza, a
  // tartalom megsem kerul ki. A File::close() es a File::flush() void, tehat
  // a lezaraskori hibat CSAK a visszaolvasas foghatja meg. Eddig ezt az agat
  // egyetlen teszt sem futtatta vegig a writeConfigValue()-n keresztul.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fs.clear();
  g_fsSilentWriteFail = true;
  const bool ok = writeConfigValue(LittleFS, "/x.txt", "fontos adat");
  g_fsSilentWriteFail = false;
  CHECK(!ok, "a 'sikeres' iras is elbukik, ha nem olvashato vissza");
  CHECK(serialHas("verify FAILED"), "es meg is mondja, miert");
}

static void scFS8() {
  // Ugyanez a beallito portalon at: ilyenkor sem szabad sikert jelenteni,
  // mert az ujraindulas utan hasznalhatatlan konfiguracioval jonne fel.
  coldBoot(false, "", "", "", "");
  setup();
  g_fsSilentWriteFail = true;
  std::string body;
  const int code = postConfig("Halozat", "jelszo123", "", "", &body);
  g_fsSilentWriteFail = false;
  CHECK(code == 500, "nema irashiba -> 500, nem hamis siker");
  CHECK(!restartPending, "es nem indul ujra hasznalhatatlan konfiggal");
}

static void scFS9() {
  // Csonka olvasas: a fajl letezik, de kevesebb bajt jon vissza, mint amennyi
  // a merete. Ilyenkor NEM szabad a csonka erteket ervenyes konfigkent venni.
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsShortRead = true;
  try { setup(); } catch (DeepSleepSignal&) {}
  g_fsShortRead = false;
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL - serult konfig");
  CHECK(serialHas("short read"), "jelzi a csonka olvasast");
  CHECK(wifiSim.beginCount == 0, "es meg csak meg sem probal csatlakozni");
}

static void scFS10() {
  // A fileMatches() ket tovabbi elbukasi modja: nem nyithato meg a fajl, es
  // maskora a merete. Egyik sem szamithat egyezesnek.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fs["/m.txt"] = "hosszabb tartalom";
  CHECK(!fileMatches(LittleFS, "/m.txt", "rovid", 5), "elteroo meret -> nem egyezik");
  CHECK(!fileMatches(LittleFS, "/nincs.txt", "barmi", 5), "hianyzo fajl -> nem egyezik");
  g_fsReadable = false;
  CHECK(!fileMatches(LittleFS, "/m.txt", "hosszabb tartalom", 17),
        "olvashatatlan fajl -> nem egyezik");
  g_fsReadable = true;
}


// --- Keep-alive: a nyitva levo lap tartsa ebren az eszkozt ------------------
static void scKA1() {
  // Merve: keep-alive nelkul 6 perc gepeles kozben elaludt, es a Submit mar
  // nem erte el az eszkozt. A lap most 60 mp-enkent jelez.
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(g_handlers.count("/ping#1") == 1, "van /ping vegpont");
  AsyncWebServerRequest r; g_handlers["/ping#1"](&r);
  CHECK(r._code == 200, "/ping -> 200");
  CHECK(r._body.size() <= 4, "a valasz aprocska (percenkent fut)");
}

static void scKA2() {
  // 20 percig nyitva a lap, 60 mp-enkent pingel: NEM alhat el.
  coldBoot(false, "", "", "", "");
  setup();
  bool slept = false;
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 20u * 60 * 1000) {
      { AsyncWebServerRequest r; g_handlers["/ping#1"](&r); }
      const uint32_t p0 = g_millis;
      while (g_millis - p0 < 60u * 1000) loop();     // 60 mp a kovetkezo pingig
    }
  } catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "20 percig nyitva tartott lap mellett NEM alszik el");
  CHECK(deviceMode == (DeviceMode)1, "vegig AP konfig modban marad");
}

static void scKA3() {
  // Ha bezarom a lapot, a pingek elmaradnak - onnan szamitva 5 perc mulva
  // alszik el. A funkcio nem "orokke ebren" kapcsolo.
  coldBoot(false, "", "", "", "");
  setup();
  { AsyncWebServerRequest r; g_handlers["/ping#1"](&r); }   // utolso ping
  const uint32_t last = g_millis;
  bool slept = false;
  try { while (g_millis - last < 10u * 60 * 1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "a lap bezarasa utan azert elalszik");
  const uint32_t el = g_millis - last;
  CHECK(el >= 5u*60*1000 && el < 5u*60*1000 + 2000,
        "pontosan az utolso ping utan 5 perccel");
}

static void scKA4() {
  // A keep-alive script mindket urlapon es a naplooldalon is ott van,
  // kulonben az egyik lapon eszrevetlenul elaludna az eszkoz.
  coldBoot(false, "", "", "", "");
  setup();
  { AsyncWebServerRequest r; g_handlers["/#1"](&r);
    CHECK(r._body.find("/ping") != std::string::npos,
          "a tartalek urlapon van keep-alive"); }
  { AsyncWebServerRequest r; g_handlers["/log#1"](&r);
    CHECK(r._body.find("/ping") != std::string::npos,
          "a naplooldalon is van keep-alive");
    CHECK(r._body.find("</body></html>") != std::string::npos,
          "es a lap rendesen le van zarva"); }
  { AsyncWebServerRequest r; g_handlers["/#1"](&r);
    CHECK(r._body.find("href=\"/log\"") != std::string::npos,
          "a tartalek urlapon ott a naplo link is"); }
}


static void scKA5() {
  // Az AP jelszo hossza WPA2-kotott. A core rovid jelszonal csak annyit tesz,
  // hogy "passphrase too short!" es return false (AP.cpp) - a softAP()
  // visszateresi erteket viszont nem nezzuk, tehat az AP NEM jonne letre, az
  // eszkoz elerhetetlen lenne, es 5 perc mulva elaludna. A sketch ezt
  // static_assert-tel fogja meg, ez a teszt a futasidoben latszo reszt orzi.
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(wifiSim.softApCount == 1, "az AP elindult");
  CHECK(wifiSim.apPass.size() >= 8 && wifiSim.apPass.size() <= 63,
        "az AP jelszo a WPA2 tartomanyban van (8-63 karakter)");
}


// --- A mentett jelszo osszekeverese ----------------------------------------
static void scSE1() {
  // Oda-vissza: barmilyen hosszra, minden hasznalhato karakterrel.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const char* minta[] = { "", "a", "nyolckar", "SzuperTitkosJelszo42",
                          "  szelso  szokozok  ", "!@#$%^&*()_+-=[]{};:,.<>?" };
  bool ok = true;
  for (const char* m : minta) {
    char enc[200]; char dec[200];
    if (!encodeSecret(m, enc, sizeof(enc))) { ok = false; break; }
    strlcpy(dec, enc, sizeof(dec));
    decodeSecretInPlace(dec);
    if (strcmp(dec, m) != 0) { ok = false; printf("     [info] elteres: '%s'\n", m); }
  }
  CHECK(ok, "minden minta hibatlanul jon vissza");

  // A leghosszabb WPA2 jelszo is belefer a pufferbe.
  std::string maxPw(63, 'Z');
  char enc[3 + 2*63 + 1];
  CHECK(encodeSecret(maxPw.c_str(), enc, sizeof(enc)), "63 karakter is belefer");
  char dec[200]; strlcpy(dec, enc, sizeof(dec)); decodeSecretInPlace(dec);
  CHECK(std::string(dec) == maxPw, "es hibatlanul jon vissza");

  // Szuk puffer: ne irjon tul, adjon false-t.
  char kicsi[8];
  CHECK(!encodeSecret("hosszabb jelszo", kicsi, sizeof(kicsi)),
        "szuk puffernel false, nem tulcsordulas");
}

static void scSE2() {
  // A `strings` ne adjon hasznalhato jelszot: se a nyilt szoveg, se annak
  // barmely 4 karakteres darabja ne legyen megtalalhato a fajlban.
  coldBoot(false, "", "", "", "");
  setup();
  const char* PW = "SzuperTitkosJelszo42";
  postConfig("Halozat", PW, "", "");
  const std::string& f = g_fs["/pass.txt"];
  CHECK(f.find(PW) == std::string::npos, "a teljes jelszo nincs a fajlban");
  bool darab = false;
  for (size_t i = 0; i + 4 <= strlen(PW); i++)
    if (f.find(std::string(PW).substr(i, 4)) != std::string::npos) darab = true;
  CHECK(!darab, "meg 4 karakteres darabja sincs");
  CHECK(f.rfind("v1:", 0) == 0, "a formatum verziozott (v1:)");
  bool csakHexa = true;
  for (size_t i = 3; i < f.size(); i++)
    if (!isxdigit((unsigned char)f[i]) || isupper((unsigned char)f[i])) csakHexa = false;
  CHECK(csakHexa, "a tobbi resz kisbetus hexa (a szoveges olvasast nem zavarja)");
}

static void scSE3() {
  // Ismetlodo karakterek ne adjanak ismetlodo bajtokat - kulonben a mintazat
  // latszana a hexdumpban.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  char enc[200];
  encodeSecret("aaaaaaaaaaaaaaaa", enc, sizeof(enc));
  std::set<std::string> parok;
  for (size_t i = 3; i + 2 <= strlen(enc); i += 2) parok.insert(std::string(enc + i, 2));
  CHECK(parok.size() >= 12, "16 azonos karakterbol legalabb 12 kulonbozo bajt lesz");
}

static void scSE4() {
  // Masik lapkan (masik eFuse MAC) a kimasolt fajl NE mukodjon.
  coldBoot(true, "TestNet", "pw", "", "");
  g_efuseMac = 0x0000A1B2C3D4E5F6ULL;
  setup();
  char enc[200];
  encodeSecret("SzuperTitkosJelszo42", enc, sizeof(enc));

  char dec[200]; strlcpy(dec, enc, sizeof(dec));
  g_efuseMac = 0x0000FFEEDDCCBBAAULL;      // MASIK lapka
  decodeSecretInPlace(dec);
  CHECK(std::string(dec) != "SzuperTitkosJelszo42",
        "masik lapkan nem adja vissza a jelszot");

  strlcpy(dec, enc, sizeof(dec));
  g_efuseMac = 0x0000A1B2C3D4E5F6ULL;      // vissza az eredetire
  decodeSecretInPlace(dec);
  CHECK(std::string(dec) == "SzuperTitkosJelszo42", "a sajat lapkajan viszont igen");
}

static void scSE5() {
  // Visszafele kompatibilitas: a regi, sima szoveges mentes tovabbra is
  // mukodjon - kulonben egy frissites hasznalhatatlanna tenne a mar
  // beallitott eszkozoket.
  coldBoot(true, "TestNet", "RegiNyiltJelszo", "", "");
  setup();
  CHECK(std::string(pass) == "RegiNyiltJelszo", "a regi nyilt mentes betoltodik");
  CHECK(wifiSim.beginCount == 1, "es csatlakozik vele");
  CHECK(deviceMode == (DeviceMode)0, "monitor mod");
}

static void scSE6() {
  // Hibas tartalom NE legyen vegzetes: a rossz jelszoval a WiFi nem jon ossze,
  // es az eszkoz a szokasos uton AP modba kerul - ez ongyogyul.
  coldBoot(true, "TestNet", "", "", "");
  g_fs["/pass.txt"] = "v1:zzzz";           // ervenytelen hexa
  setup();
  CHECK(deviceMode != (DeviceMode)2, "NEM vegzetes hiba");
  CHECK(std::string(pass) == "v1:zzzz", "sima szovegkent kezeli");

  // Paratlan hosszusagu hexa is: nem a mi formatumunk.
  coldBoot(true, "TestNet", "", "", "");
  g_fs["/pass.txt"] = "v1:abc";
  setup();
  CHECK(deviceMode != (DeviceMode)2, "paratlan hosszu hexa sem vegzetes");
}

static void scSE7() {
  // Vegponttol vegpontig: mentes -> ujraindulas -> csatlakozas a helyes
  // jelszoval. Ez a lenyeg: a felhasznalonak semmit nem szabad eszrevennie.
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("OtthoniWifi", "SzuperTitkosJelszo42", "", "") == 200, "mentes OK");
  auto mentett = g_fs;
  coldBoot(true, "", "", "", "");
  g_fs = mentett;                          // ugyanaz a fajlrendszer
  setup();
  CHECK(std::string(ssid) == "OtthoniWifi", "SSID betoltve");
  CHECK(std::string(pass) == "SzuperTitkosJelszo42", "jelszo helyesen visszafejtve");
  CHECK(deviceMode == (DeviceMode)0, "monitor mod - csatlakozott");
}


static void scSE8() {
  // A dontő kerdes: a RADIOIG a nyilt jelszo jut-e el, es nem a fajlban
  // tarolt kodolt forma. Eddig a stub eldobta a jelszot, tehat ezt semmi
  // nem ellenorizte.
  const char* PW = "SzuperTitkosJelszo42";
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("OtthoniWifi", PW, "", "") == 200, "mentes OK");
  auto mentett = g_fs;

  coldBoot(true, "", "", "", "");
  g_fs = mentett;
  setup();
  CHECK(wifiSim.lastSsid == "OtthoniWifi", "a WiFi.begin() a helyes SSID-t kapja");
  CHECK(wifiSim.lastPass == PW, "a WiFi.begin() a NYILT jelszot kapja");
  CHECK(wifiSim.lastPass.rfind("v1:", 0) != 0, "nem a kodolt format kapja");
  CHECK(deviceMode == (DeviceMode)0, "csatlakozott, monitor mod");
}

static void scSE9() {
  // Nem csak az induláskori csatlakozas: a kapcsolat kieses utani
  // ujracsatlakozas is a helyes jelszoval megy.
  const char* PW = "MasikTitok99";
  coldBoot(false, "", "", "", "");
  setup();
  postConfig("OtthoniWifi", PW, "", "");
  auto mentett = g_fs;

  coldBoot(true, "", "", "", "");
  g_fs = mentett;
  setup();
  g_httpBody = "Microsoft Connect Test";
  loop();                                   // firstStart lezarasa
  const int elozo = wifiSim.beginCount;
  wifiSim.begun = false;                    // a kapcsolat kiesik
  wifiSim.lastPass.clear();
  int guard = 0;
  try { while (++guard < 100000 && !serialHas("WIFI RECONNECTED!")) loop(); }
  catch (...) {}
  CHECK(wifiSim.beginCount > elozo, "ujra probalt csatlakozni");
  CHECK(wifiSim.lastPass == PW, "az ujracsatlakozas is a nyilt jelszoval megy");
  CHECK(serialHas("WIFI RECONNECTED!"), "es sikerult");
}

static void scSE10() {
  // Router reset utani ujracsatlakozas: itt a WiFi.disconnect(true) eldobja a
  // netifet, tehat teljes initWiFi() fut ujra - a jelszonak ott is jonek kell
  // lennie.
  const char* PW = "HarmadikTitok7";
  coldBoot(false, "", "", "", "");
  setup();
  postConfig("OtthoniWifi", PW, "", "");
  auto mentett = g_fs;

  coldBoot(true, "", "", "", "");
  g_fs = mentett;
  setup();
  g_httpBody = "Rossz"; pingSim.ok = false;   // az internet nem megy
  loop();
  // A rele pulzus EGYETLEN loop() hivason belul zajlik (a reset_device()-t
  // blokkolo ciklus hajtja), tehat a loop() utan mar ujra LOW. A naplobol
  // kell nezni, hogy volt-e HIGH.
  int guard = 0;
  try { while (++guard < 400000 && logIndex(RELAY_HIGH) < 0) loop(); }
  catch (...) {}
  CHECK(logIndex(RELAY_HIGH) >= 0, "router reset elindult");
  wifiSim.lastPass.clear();
  try { while (++guard < 900000 && !serialHas("WIFI OK in FAILURE_STATE.")) loop(); }
  catch (...) {}
  CHECK(wifiSim.lastPass == PW, "a router reset utani csatlakozas is a nyilt jelszoval megy");
}


// --- Elgepelt statikus IP: a gateway sem valaszol ---------------------------
// A Wi-Fi TARSITAS sikerul (WPA2, 2. reteg), de IP szinten nincs ut. A router
// ujrainditasa ezt sosem javitja - kap egy eselyt, aztan AP mod.
static int gwFutas(bool gwElerheto, bool statikus, uint32_t maxKor) {
  coldBoot(true, "TestNet", "pw",
           statikus ? "192.168.0.200" : "", statikus ? "192.168.0.1" : "");
  setup();
  g_httpBody = "Rossz";                       // az internet nem megy
  pingSim.ok = false;                         // 1.1.1.1 / 8.8.8.8 sem
  pingSim.perTarget["192.168.0.1"] = gwElerheto;
  loop();
  int guard = 0, resetek = 0; bool magas = false;
  try {
    while ((uint32_t)++guard < maxKor && deviceMode != (DeviceMode)1) {
      const size_t before = g_log.size();
      loop();
      for (size_t i = before; i < g_log.size(); i++) {
        if (g_log[i] == RELAY_HIGH && !magas) { magas = true; resetek++; }
        if (g_log[i] == RELAY_LOW) magas = false;
      }
    }
  } catch (...) {}
  return resetek;
}

static void scGW1() {
  // A gateway VALASZOL: a hiba nem helyi, marad a megszokott viselkedes.
  const int resetek = gwFutas(true, true, 400000);
  CHECK(deviceMode != (DeviceMode)1, "NEM megy AP modba");
  CHECK(resetek >= 2, "tovabbra is ujraindítja a routert");
}

static void scGW2() {
  // A gateway NEM valaszol: egy reset, aztan AP mod.
  const int resetek = gwFutas(false, true, 400000);
  CHECK(resetek == 1, "pontosan EGY router reset (kap egy eselyt)");
  CHECK(deviceMode == (DeviceMode)1, "utana AP beallito mod");
  CHECK(serialHas("Valoszinuleg rossz a statikus IP"), "meg is mondja, miert");

  bool elso = false, masodik = false, apOk = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 12 && rtcEvents[i].param == 1) elso = true;
    if (rtcEvents[i].code == 12 && rtcEvents[i].param == 2) masodik = true;
    if (rtcEvents[i].code == 6  && rtcEvents[i].param == 4) apOk = true;
  }
  CHECK(elso, "naplo: GW UNREACH a reset elott (param=1)");
  CHECK(masodik, "naplo: GW UNREACH a reset utan is (param=2)");
  CHECK(apOk, "naplo: AP MODE a gateway ok miatt (param=4)");
}

static void scGW3() {
  // DHCP-nel NINCS mit ellenorizni: a gateway magatol a routertol jott.
  // Ilyenkor a viselkedes valtozatlan - vegtelen ujraprobalkozas.
  const int resetek = gwFutas(false, false, 400000);
  CHECK(deviceMode != (DeviceMode)1, "DHCP-nel nem megy AP modba");
  CHECK(resetek >= 2, "a megszokott modon ujraindítja a routert");
  CHECK(!serialHas("sajat gateway"), "meg csak nem is pingeli a gateway-t");
}


static void scP16() {
  // A validacio a KOZOS POST kezeloben van, tehat mindket urlapra ugyanugy
  // ervenyes. Ez a teszt ezt rogziti, hogy senki ne tudja szetcsuszani oket.
  struct { const char* ip; const char* gw; int varhato; } esetek[] = {
    { "192.168.1.300", "192.168.1.1", 500 },   // 300-as oktett
    { "192.168..1.20", "192.168.1.1", 500 },   // dupla pont
    { "192.168.1.200", "192.168.1",   500 },   // hianyzo oktett
    { "192.168.1.abc", "192.168.1.1", 500 },   // betu a cimben
    { "fe80::1",       "192.168.1.1", 500 },   // IPv6
    { "0.0.0.0",       "192.168.1.1", 500 },   // DHCP-nek szamitana
    { "192.168.1.200", "",            500 },   // felig kitoltott paros
    { "192.168.1.200", "192.168.1.1", 200 },   // helyes
  };
  // Urlapbol mar csak EGY van (CONFIG_FORM, a programba forditva), tehat nincs
  // ket agat szetcsuszni. A validacio viszont ugyanaz a kozos POST kezelo,
  // ezert a lista tovabbra is ertelmes regresszio.
  bool mind = true;
  for (auto& e : esetek) {
    coldBoot(false, "", "", "", "");
    // Szandekosan felteszunk egy regi HTML-t is: ha valaki visszavezetne a
    // fajlkiszolgalast, ez a teszt azonnal mast latna.
    g_fs["/wifimanager.html"] = "<html>REGI</html>";
    setup();
    AsyncWebServerRequest g; g_handlers["/#1"](&g);
    if (g._body.find("REGI") != std::string::npos) mind = false;
    if (postConfig("Halozat", "jelszo123", e.ip, e.gw) != e.varhato) {
      mind = false;
      printf("     [info] elteres: ip='%s' gw='%s'\n", e.ip, e.gw);
    }
  }
  CHECK(mind, "a beepitett urlapon minden validacios eset egyezik");
}


// --- Fajliras kozben SEM alvas, SEM ujrainditas ----------------------------
static uint32_t g_saveEndsAt = 0;
static void saveFinisher() {
  if (g_saveEndsAt && g_millis >= g_saveEndsAt) { savingConfig = false; g_saveEndsAt = 0; }
}

static void scWR1() {
  // A turelmi ido (2 mp) alatt UJABB mentes erkezhet - mobilon a dupla
  // koppintas gyakori. Ilyenkor eppen fajliras folyik, es a halasztott
  // ujrainditas felbe vagna.
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "", "") == 200, "elso mentes OK");
  CHECK(restartPending, "ujrainditas beutemezve");

  // Masodik mentes indul. FONTOS: a turelmi idon (2 mp) TUL fejezodjon be,
  // kulonben a varakozo ag el sem indulna - a restart check addig nem fut.
  savingConfig = true;
  g_saveEndsAt = g_millis + 2500;
  g_onDelay = saveFinisher;

  bool restarted = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 500; i++) loop(); }
  catch (RestartSignal&) { restarted = true; }
  g_onDelay = nullptr;

  CHECK(restarted, "vegul ujraindul");
  CHECK(!savingConfig, "de csak azutan, hogy az iras befejezodott");
  CHECK(g_millis - t0 >= 2500, "megvarta a teljes irast");
  CHECK(serialHas("Fajliras folyik"), "jelzi is, hogy var");
  CHECK(serialHas("A fajliras befejezodott"), "es hogy kesz");
}

static void scWR2() {
  // Ugyanez az alvasra: az AP hatarido lejar, de eppen mentes folyik.
  coldBoot(false, "", "", "", "");
  setup();
  apDeadline = g_millis;          // a hatarido MOST jart le
  savingConfig = true;
  g_saveEndsAt = g_millis + 800;
  g_onDelay = saveFinisher;

  bool slept = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 500; i++) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  g_onDelay = nullptr;

  CHECK(slept, "vegul elalszik");
  CHECK(!savingConfig, "de csak a fajliras utan");
  CHECK(g_millis - t0 >= 800, "megvarta a teljes irast");
}

static void scWR3() {
  // A varakozas KORLATOS: ha a jelzo barmiert beragadna, az eszkoz nem
  // fagyhat le miatta - 5 mp utan tovabblep, es szol rola.
  coldBoot(false, "", "", "", "");
  setup();
  postConfig("Halozat", "jelszo123", "", "");
  savingConfig = true;            // szandekosan sosem tisztul
  bool restarted = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 5000; i++) loop(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "beragadt jelzo eseten is ujraindul");
  CHECK(g_millis - t0 >= 5000, "de csak az 5 mp-es hatarido utan");
  CHECK(g_millis - t0 < 8000, "es nem var a vegtelensegig");
  CHECK(serialHas("sem fejezodott be"), "figyelmeztet a beragadt jelzore");
  savingConfig = false;
}


// --- millis() korbefordulas (49,7 naponta) ---------------------------------
// A stub millis()-e SZANDEKOSAN uint32_t: a hoston az `unsigned long` 64 bites
// lenne, es akkor a "millis() - start" idiomak maskepp viselkednenek, mint az
// ESP32-C3-on. Enelkul ezek a tesztek ertelmetlenek lennenek.
static uint32_t mw_pulzus = 0;
static bool     mw_magas = false;
static uint32_t mw_start = 0;
static int      mw_wrapok = 0;
static uint32_t mw_elozo = 0;
static void mwFigyelo() {
  if (g_millis < mw_elozo) mw_wrapok++;
  mw_elozo = g_millis;
  const bool m = (g_pinState[PIN_RELAY] == HIGH);
  if (m && !mw_magas) { mw_magas = true; mw_start = g_millis; }
  if (!m && mw_magas) { mw_magas = false; mw_pulzus = g_millis - mw_start; }
}
static void wrapFutas(uint32_t kezdo) {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpBody = "Microsoft Connect Test";
  loop(); loop();
  g_millis = kezdo;
  loop(); loop();
  wifiSim.begun = false; wifiSim.willConnect = false;
  wifiSim.failStatus = WL_NO_SSID_AVAIL;
  mw_pulzus = 0; mw_magas = false; mw_wrapok = 0; mw_elozo = g_millis;
  g_onDelay = mwFigyelo;
  try { int g = 0; while (++g < 3000000 && deviceMode != (DeviceMode)1) loop(); }
  catch (...) {}
  g_onDelay = nullptr;
}

static void scMW1() {
  // A rele pulzus a projekt legkritikusabb idozitese - az eredeti fo hiba is
  // itt volt. Ha a wrap EPP a pulzus alatt tortenik, akkor is 90 mp legyen.
  wrapFutas(0xFFFFFFFFu - 240000u);
  CHECK(mw_wrapok == 1, "a millis() tenyleg korbefordult kozben");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000,
        "a rele pulzus a korbefordulas alatt is 90 mp");
}

static void scMW2() {
  // Wrap a 10 perces RESET_DELAY alatt, illetve a hibakezeles kozepen.
  wrapFutas(0xFFFFFFFFu - 700000u);
  CHECK(mw_wrapok == 1, "korbefordult a RESET_DELAY alatt");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000, "a pulzus akkor is 90 mp");
  CHECK(g_wakeupUs == 3600000000ULL, "a vegen ugyanugy 1 ora alvas");

  wrapFutas(0xFFFFFFFFu - 180000u);
  CHECK(mw_wrapok == 1, "korbefordult a hibakezeles kozepen");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000, "a pulzus akkor is 90 mp");
}

static void scMW3() {
  // 169 nap = 3 teljes korbefordulas mogotte. A viselkedes valtozatlan.
  wrapFutas(1716698112u);                    // 169 nap % 2^32
  CHECK(mw_wrapok == 0, "ebben a szakaszban nincs wrap");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000, "90 mp-es pulzus 169 nap utan is");
  CHECK(rtcRetryRounds == 1, "a szamlalo normalisan lep");
  CHECK(g_wakeupUs == 3600000000ULL, "1 ora alvas");
}


// --- Watchdog: a leghosszabb etetes nelkuli szakasz -------------------------
// A valodi loopTask (main.cpp:79-82) MINDEN loop() elott etet, ha a loop task
// fel van iratkozva - ezt modellezzuk. A stub ping/HTTP mostmar a valodi
// timeoutig "blokkol", kulonben ez a meres semmit nem erne.
static void wdLepes() { if (g_wdtEnabled) feedLoopWDT(); loop(); }
static uint32_t wdMeres(uint32_t futasMs) {
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  const uint32_t t0 = g_millis;
  try { int g = 0; while (++g < 3000000 && g_millis - t0 < futasMs) wdLepes(); }
  catch (...) {}
  g_wdtTrack = false;
  return g_wdtMaxFeedGap;
}

static void scWD7() {
  // A legrosszabb eset: minden HTTP keres TIMEOUTBA fut. A http.GET() ilyenkor
  // a connect (5 mp) + valasz (10 mp) timeoutig blokkol, etetes nelkul - a
  // 90 mp-es watchdog timeout epp erre volt meretezve.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpCode = -1; pingSim.ok = false;
  loop();
  const uint32_t gap = wdMeres(60u * 60 * 1000);
  printf("     [info] leghosszabb etetes nelkuli szakasz: %.1f mp\n", gap/1000.0);
  CHECK(gap >= 14000, "a HTTP timeout tenyleg blokkol (a meres ervenyes)");
  CHECK(gap < 90000, "es a 90 mp-es watchdog timeout alatt marad");
}

// A DNS a connect timeouton KIVUL esik, ezert a halott nevszerver rosszabb,
// mint a hallgato szerver. Realis legrosszabb eset (2 DNS szerver + globalis
// IPv6, tehat ketszeres getaddrinfo, plusz a 0.0.0.0-ra iranyulo connect):
//   2 x (2 x 7 mp) + 5 mp = 33 mp egyetlen bukott HTTP tesztre.
// Epp ez az uj eszkalacio alapesete: mind az ot teszt nevfeloldast igenyel.
static void scWD13() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpCode = -1;
  g_httpFailMs = 33000;   // halott DNS, nem hallgato szerver
  loop();
  const uint32_t gap = wdMeres(60u * 60 * 1000);
  g_httpFailMs = 15000;
  printf("     [info] halott DNS mellett a leghosszabb szakasz: %.1f mp\n", gap/1000.0);
  CHECK(gap >= 32000, "a 33 mp-es DNS blokkolas tenyleg beleszamit (a meres ervenyes)");
  CHECK(gap < 90000, "es meg igy is a 90 mp-es watchdog timeout alatt marad");
}

static void scWD8() {
  // Elgepelt statikus IP: a gateway pingje is beleszamit a szakaszba.
  coldBoot(true, "TestNet", "pw", "192.168.0.9", "192.168.0.1");
  setup();
  g_httpCode = -1; pingSim.ok = false;
  pingSim.perTarget["192.168.0.1"] = false;
  loop();
  const uint32_t gap = wdMeres(40u * 60 * 1000);
  printf("     [info] leghosszabb etetes nelkuli szakasz: %.1f mp\n", gap/1000.0);
  CHECK(gap < 90000, "gateway-ellenorzessel egyutt is 90 mp alatt");
}

static void scWD9() {
  // A tobbi uzemmod: AP portal, vegzetes hiba, first start varakozas.
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(wdMeres(5u * 60 * 1000) < 90000, "AP konfig modban is 90 mp alatt");

  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  CHECK(wdMeres(5u * 60 * 1000) < 90000, "vegzetes hibanal is 90 mp alatt");

  coldBoot(false, "TestNet", "pw", "", "");
  wifiSim.availableFrom = 25u * 60 * 1000;
  setup();
  CHECK(wdMeres(25u * 60 * 1000) < 90000, "first start varakozas alatt is 90 mp alatt");
}


static void scWD10() {
  // A watchdog mar a Wi-Fi indulasa ELOTT el, nem csak a setup() vegen.
  // Egy beragadt WiFi.begin() korabban orokre megallitotta volna az eszkozt.
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtEnabled = false;
  // A LittleFS csatolasa utan, de a Wi-Fi elott kell elesednie: ezt ugy
  // merjuk, hogy a WiFi.begin() pillanataban mar aktivnak kell lennie.
  setup();
  const int iWdt   = logIndex("enableLoopWDT");
  const int iBegin = logIndex("WiFi.begin(");
  const int iFs    = logIndex("wdt_reconfigure");
  CHECK(iWdt >= 0, "a watchdog feliratkozas megtortent");
  CHECK(iBegin >= 0, "a WiFi.begin() lefutott");
  CHECK(iWdt < iBegin, "a watchdog ELOBB elesedik, mint a WiFi.begin()");
  CHECK(iFs >= 0, "es konfiguralva is lett");
}

static void scWD11() {
  // A LittleFS formazasa (elso indulas) szandekosan a felugyeleten KIVUL van:
  // egy lassu formazas soha ne futhasson watchdog resetbe.
  coldBoot(false, "", "", "", "");
  g_wdtEnabled = false;
  g_wdtFeedNotSubscribed = 0;
  setup();
  CHECK(g_wdtFeedNotSubscribed == 0, "a felelesztes elott nem etetunk");
  CHECK(g_wdtEnabled, "a setup() vegere aktiv a watchdog");
  CHECK(g_wdtTimeoutMs == 90000 && g_wdtPanic, "90 mp, panic bekapcsolva");
}

static void scWD12() {
  // Az elesedes utan a setup() hatralevo resze sem lephet 90 mp fole.
  // A leghosszabb ott az initWiFi() 20 mp-es varakozasa, ami etet.
  coldBoot(false, "TestNet", "pw", "", "");   // nem tud csatlakozni -> 20 mp
  setup();
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  coldBoot(false, "TestNet", "pw", "", "");
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  setup();
  g_wdtTrack = false;
  printf("     [info] setup() leghosszabb etetes nelkuli szakasza: %.1f mp\n",
         g_wdtMaxFeedGap/1000.0);
  CHECK(g_wdtMaxFeedGap < 90000, "a setup() is 90 mp alatt marad");
}

static void scS5() {
  // Ébredés után a setup() előbb maga hajtja LOW-ra a relét, és csak utána
  // oldja fel az alvás előtti holdot - a láb egy pillanatra sem marad magára.
  coldBoot(true, "TestNet", "pw", "", "", 500, true);   // deep sleep ébredés
  g_heldPins.insert(PIN_RELAY); g_deepSleepHoldEnabled = true;  // az alvás előtti hold
  setup();
  CHECK(g_heldPins.empty(), "a hold ébredés után feloldva");
  CHECK(!g_deepSleepHoldEnabled, "a globális deep sleep hold kikapcsolva");
  { const int drive = logIndex(RELAY_LOW);
    const int release = logIndex("gpio_hold_dis(10)");
    CHECK(drive >= 0 && release >= 0 && drive < release,
          "előbb a meghajtott LOW, aztán a hold feloldása"); }
}

static void scS6() {
  // Ha a hold rögzítése hibázik, az alvás attól még megtörténik, és a hiba
  // nem néma - a külső lehúzó ellenállás a dokumentált tartalék.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_gpioHoldFails = true;
  bool slept = false;
  try {
    for (int c = 0; c < 10; c++) {
      int guard = 0;
      while (!reset_device() && ++guard < 200000) { yield(); }
    }
  } catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "az alvás a hold hibája ellenére megtörténik");
  CHECK(!g_deepSleepHoldEnabled, "hibás hold_en után nincs deep sleep hold");
  CHECK(serialHas("FIGYELEM: a rele lab rogzitese"), "a hiba nem néma");
}

static void scSB5() {
  // Az ismétlődő beragadt-gomb ébredés nem árasztja el a naplót: az első kör
  // (BOOT + STUCK BUTTON) bekerül, az ismétlések némák - különben a 60 mp-es
  // körforgás fél óra alatt kiszorítaná a kivizsgálandó eseményeket.
  coldBoot(true, "TestNet", "pw", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  g_pinRead[2] = LOW;                        // wifireset beragadva
  try { setup(); } catch (DeepSleepSignal&) {}
  const uint32_t afterFirst = rtcEvNext;
  CHECK(afterFirst == 2, "az első kör két bejegyzés: BOOT + STUCK BUTTON");
  for (int wake = 0; wake < 5; wake++) {     // öt további ébredés, még beragadva
    coldBoot(true, "TestNet", "pw", "", "", 500, true);
    g_pinRead[2] = LOW;
    try { setup(); } catch (DeepSleepSignal&) {}
  }
  CHECK(rtcEvNext == afterFirst, "az ismétlődő ébredések nem írnak új bejegyzést");
  coldBoot(true, "TestNet", "pw", "", "", 500, true);   // a gomb kiszabadult
  setup();
  CHECK(rtcEvNext > afterFirst, "a kiszabadulás utáni boot újra naplóz");
}

static void scP17() {
  // Másolás-beillesztés szóközei: az IP/gateway mező is vágva validálódik,
  // ugyanúgy, ahogy az SSID és a jelszó.
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig("MyNetwork", "jelszo", " 192.168.1.200 ", "192.168.1.1 ");
  CHECK(code == 200, "szóközös IP/gateway elfogadva (vágás után)");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200", "az IP vágva mentődik");
  CHECK(g_fs["/gateway.txt"] == "192.168.1.1", "a gateway vágva mentődik");
  // A vágás nem válhat csonkolássá: egy 31 karakternél hosszabb érték végét a
  // puffer levágná, és a maradék akár érvényes címmé is "vágódhatna" - az ilyen
  // bemenet hiba, nem csendben megtisztított érték. (Az űrlap maxlength=15-je
  // ezt böngészőből nem engedi, kézzel gyártott POST-ból jöhet.)
  const char* oversize = "192.168.1.200                    X";  // 34 karakter
  const int code2 = postConfig("MyNetwork", "jelszo", oversize, "192.168.1.1");
  CHECK(code2 == 500, "túlméretes IP érték hiba, nem csonkolt elfogadás");
}

static void scP18() {
  // Csupa szóköz mező = üres mező = DHCP, nem hibaüzenet.
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig("MyNetwork", "jelszo", "   ", "  ");
  CHECK(code == 200, "csupa szóköz IP/gateway nem hiba");
  CHECK(g_fs["/ip.txt"].empty() && g_fs["/gateway.txt"].empty(), "DHCP-ként mentve");
}

static void scP19() {
  // A konfigfájloknak egy írója lehet: ha a zár (savingConfig) másnál van -
  // épp a wifireset gomb töröl -, a webes mentés 503-mal hátrál, fájlt nem ír.
  coldBoot(false, "", "", "", "");
  setup();
  savingConfig = true;                     // a másik író épp dolgozik
  const int code = postConfig("MyNetwork", "jelszo", "", "");
  CHECK(code == 503, "zárolt konfignál 503, nem néma felülírás");
  CHECK(!g_fs.count("/ssid.txt"), "fájl nem íródott");
  CHECK(!restartPending, "újraindítás sincs beütemezve");
  CHECK(savingConfig, "a más által tartott zárat nem engedte el");
  savingConfig = false;
  const int code2 = postConfig("MyNetwork", "jelszo", "", "");
  CHECK(code2 == 200, "a zár felszabadulása után a mentés már lefut");
}

static void scP20() {
  // Mentett statikus IP mellett egy csak SSID+jelszó POST érvényes: a
  // párosság-ellenőrzés a VÉGSŐ értékpárra vonatkozik (a nem küldött mező a
  // mentett értékén marad), és az ip/gateway fájlokhoz hozzá sem nyúlunk.
  coldBoot(false, "", "", "", "");
  setup();                                     // AP portál fut
  // Korábbi mentés maradéka: statikus pár a fájlokban ÉS a futó globálisokban.
  strlcpy(ipStr, "192.168.1.200", 16);
  strlcpy(gatewayStr, "192.168.1.1", 16);
  g_fs["/ip.txt"] = "192.168.1.200";
  g_fs["/gateway.txt"] = "192.168.1.1";
  const int code = postConfig("UjHalozat", "ujjelszo", nullptr, nullptr);
  CHECK(code == 200, "SSID+jelszó csere statikus IP mellett érvényes");
  CHECK(g_fs["/ssid.txt"] == "UjHalozat", "az SSID frissült");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200" && g_fs["/gateway.txt"] == "192.168.1.1",
        "a nem küldött IP/gateway fájlok érintetlenek");
}


// ===========================================================================
// UJ VISELKEDES: korai kilepes a hosszu varakozasokbol (onlineProbe)
// ===========================================================================

// Segedletek: hany g_log bejegyzes utal fajlrendszer-irasra a proba alatt.
static int fsIrasokSzama() {
  int n = 0;
  for (auto& l : g_log) {
    if (l.rfind("fs_write", 0) == 0 || l.rfind("fs_open_w", 0) == 0) n++;
  }
  return n;
}

static void scOP1() {
  // A halozat 3 perc mulva jon vissza, es az internet is megy: a
  // firstStartDelay NEM fut vegig, hanem a proba lezarja.
  coldBoot(true, "MyNetwork", "titok123", "", "");
  wifiSim.availableFrom = 3u * 60 * 1000;   // a router 3 perc mulva all fel
  pingSim.ok = true;
  setup();                                   // az initWiFi() itt meg bukik
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (uiFlags.firstStart && ++guard < 400000) loop(); }
  catch (DeepSleepSignal&) {}
  const uint32_t dt = g_millis - t0;
  CHECK(!uiFlags.firstStart, "a first start varakozas lezarult");
  CHECK(dt < 5u * 60 * 1000, "es NEM a teljes 10 percet varta ki");
  CHECK(dt >= 3u * 60 * 1000, "de megvarta, amig a halozat tenyleg visszajott");
  CHECK(serialHas("halozat es internet visszajott"), "a korai kilepes meg is jelenik a naplon");
  CHECK(!pingSim.targets.empty() && pingSim.targets.back() == "1.1.1.1",
        "a masodik lepes a fix IP-re menő ping volt");
}

static void scOP2() {
  // Ugyanaz, de az INTERNET nem jon vissza (a ping bukik). Ilyenkor a
  // varakozas szabalyosan vegigfut - ez a felhasznaloi kikotes masik fele.
  coldBoot(true, "MyNetwork", "titok123", "", "");
  wifiSim.availableFrom = 3u * 60 * 1000;
  pingSim.ok = false;                        // halozat igen, internet nem
  setup();
  // A firstStart oraja a setup() ELEJEN indul (timing.startMillis), nem a
  // setup() utan - kozben lefut az initWiFi() 20 masodperces timeoutja is.
  const uint32_t t0 = timing.startMillis;
  int guard = 0;
  try { while (uiFlags.firstStart && ++guard < 400000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(g_millis - t0 >= 10u * 60 * 1000, "a teljes 10 perc lefutott");
  CHECK(!serialHas("halozat es internet visszajott"), "nem volt korai kilepes");
  CHECK(!pingSim.targets.empty(), "a proba azert probalkozott");
}

static void scOP3() {
  // Sikeres indulasnal az ELSO proba azonnal fusson: enelkul minden ep
  // bekapcsolas 60 masodpercet kesne az elso internettesztig. (Regresszio:
  // az elso valtozat pontosan igy viselkedett.)
  coldBoot(true, "MyNetwork", "titok123", "", "");
  pingSim.ok = true;
  setup();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (uiFlags.firstStart && ++guard < 100000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(!uiFlags.firstStart, "azonnal lezarult");
  CHECK(g_millis - t0 < 30u * 1000, "masodpercek alatt, nem egy teljes proba-utem mulva");
}

static void scOP4() {
  // FLASH KIMELES: a proba semmit nem ir a fajlrendszerre, es a begin()-ek
  // szama a 60 mp-es utemhez kotott. A WiFi.persistent(false) a setup()-ban
  // fut le, tehat a begin() nem ir NVS-be.
  coldBoot(true, "MyNetwork", "titok123", "", "");
  wifiSim.availableFrom = 0xFFFFFFFFu;       // a halozat sosem jon vissza
  pingSim.ok = false;
  setup();
  const int beginBefore = wifiSim.beginCount;
  const size_t fsBefore = g_fs.size();
  // EZ a flash-kimeles alapja: persistent(false) mellett a WiFi.begin() nem
  // ir NVS-be. Ha ez a hivas eltunne, a proba percenkent irna a flasht.
  CHECK(logIndex("WiFi.persistent(false)") >= 0,
        "a setup() persistent(false)-t hivott - a begin() nem ir NVS-be");
  g_log.clear();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 9u * 60 * 1000 && ++guard < 400000) loop(); }
  catch (DeepSleepSignal&) {}
  const uint32_t percek = (g_millis - t0) / 60000;
  CHECK(wifiSim.beginCount - beginBefore <= (int)percek + 2,
        "percenkent legfeljebb egy WiFi.begin() a probabol");
  CHECK(g_fs.size() == fsBefore, "a proba egyetlen fajlt sem hozott letre");
  CHECK(fsIrasokSzama() == 0, "es egyetlen fajlirast sem inditott");
  CHECK(pingSim.calls == 0,
        "kapcsolat nelkul NEM pingelunk - a ping csak a masodik lepes");
}

// ===========================================================================
// GOMBOK: mekkora a leghosszabb ablak, amiben egyik gombot sem nezzuk?
// ===========================================================================

// A mert legrosszabb eset a hosszu varakozasokban. A hatarok azt rogzitik,
// hogy a MI ciklusaink 10 ms-onkent mintavetelezik a gombokat - egy nagyobb
// ertek azt jelentene, hogy egy varakozas kimaradt a lefedettsegbol.
static uint32_t merjGombHezag(void (*forgatokonyv)()) {
  g_btnTrack = true; g_btnLastPoll = g_millis; g_btnMaxGap = 0;
  forgatokonyv();
  g_btnTrack = false;
  return g_btnMaxGap;
}

static void scBTN1() {
  // A HOSSZU VARAKOZASOK alatt mindket gombot 10 ms-onkent nezzuk.
  // Vegigjatszunk egy teljes first start varakozast + router resetet +
  // RESET_DELAY-t, es megmerjuk a leghosszabb "vak" ablakot.
  //
  // A HTTP kereseket szandekosan GYORSRA allitjuk: a blokkolo http.GET() a
  // core-e, azt nem tudjuk megszakitani, es kulon teszt (BTN2) meri. Itt a
  // MI ciklusaink a targy.
  coldBoot(false, "MyNetwork", "titok123", "", "");
  g_httpOkMs = 10; g_httpFailMs = 10; pingSim.okMs = 10; pingSim.failMs = 10;
  setup();
  const uint32_t gap = merjGombHezag([]() {
    const uint32_t t0 = g_millis;
    int guard = 0;
    try { while (g_millis - t0 < 20u*60*1000 && ++guard < 500000) loop(); }
    catch (DeepSleepSignal&) {}
  });
  printf("     [info] leghosszabb gomb-vak ablak: %u ms\n", gap);
  CHECK(gap <= 60, "a sajat varakozo ciklusaink 10 ms-onkent nezik a gombokat");
}

static void scBTN2() {
  // A blokkolo HTTP keres alatt a gombok NEM nezhetok: a http.GET() a core
  // fuggvenye, nincs benne visszahivas. Ez nem hiba, hanem a konyvtar
  // korlatja - de MERT ertek, hogy tudjuk, mit dokumentalunk.
  //
  // A legrosszabb eset a HALOTT DNS: 33 mp (lasd WDT_TIMEOUT_MS levezeteset).
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1;
  g_httpFailMs = 33000;              // halott DNS: a legrosszabb eset
  pingSim.ok = true;
  setup();
  const uint32_t gap = merjGombHezag([]() {
    int guard = 0;
    try {
      while (++guard < 200000 && !serialHas("Test failed.")) loop();
      // MEG EGY kor: a hezag csak a KOVETKEZO gombolvasaskor rogzul, es a
      // blokkolo kerest tartalmazo loop() mar nem olvas gombot. Enelkul a
      // meres 60 ms-ot mutatna, vagyis eltakarna a vizsgalt jelenseget.
      loop();
    } catch (DeepSleepSignal&) {}
  });
  printf("     [info] a blokkolo HTTP keres alatti vak ablak: %u ms\n", gap);
  CHECK(gap >= 30000, "a http.GET() alatt tenyleg nem nezzuk a gombokat");
  CHECK(gap <= 34000,
        "de csak EGY keres erejeig - a keresek KOZOTT ujra pollozunk");
}

static void scBTN3() {
  // A gombot a vak ablak UTAN is fel kell ismerni: a debounce nem "veszik el"
  // attol, hogy kozben nem mintavetelezunk. Egy VEGIG nyomva tartott gomb a
  // pollozas visszaterese utan ket mintaval (>= 50 ms) ujrainditja az eszkozt.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; g_httpFailMs = 33000; pingSim.ok = true;
  setup();
  g_pinRead[3] = LOW;                // a reset gombot VEGIG nyomva tartjuk
  bool restarted = false;
  int guard = 0;
  try { while (++guard < 200000 && !restarted) loop(); }
  catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) {}
  CHECK(restarted, "a vegig nyomva tartott reset gomb a vak ablak utan is hat");
}

static void scOP7() {
  // A halozat VEGIG el (az initWiFi() sikerult), csak az internet nincs meg
  // induláskor - de MENET KOZBEN visszajon. A varakozas ilyenkor NEM fut
  // vegig: a 60 mp-enkenti proba elkapja, es nem sokkal az internet
  // visszaterese utan lezarul.
  //
  // Ez a "sikeres csatlakozas utan is lefut a firstStart" allitas masik fele:
  // a 10 perc CSAK akkor telik le teljesen, ha az internet vegig hianyzik.
  coldBoot(true, "TestNet", "pw", "", "");
  pingSim.ok = false;                    // Wi-Fi megvan, internet nincs
  setup();
  const uint32_t t0 = timing.startMillis;
  const uint32_t netVissza = 210u * 1000;   // 3,5 perc mulva lesz net
  int guard = 0;
  try {
    while (uiFlags.firstStart && ++guard < 400000) {
      if (g_millis - t0 >= netVissza) pingSim.ok = true;
      loop();
    }
  } catch (DeepSleepSignal&) {}
  const uint32_t dt = g_millis - t0;
  printf("     [info] a firstStart %u mp alatt zarult le\n", dt / 1000);
  CHECK(!uiFlags.firstStart, "a varakozas lezarult");
  CHECK(dt >= netVissza, "nem elobb, mint hogy az internet visszajott");
  CHECK(dt < netVissza + 90u * 1000,
        "es az internet visszaterese utan egy proba-utemen belul (nem 10 perc)");
  CHECK(serialHas("halozat es internet visszajott"), "a korai kilepes agan zarult");
}

static void scOP6() {
  // A millis() 49,7 naponta korbefordul. A proba orai ELOJEL NELKULI
  // kivonassal dolgoznak, ezert ezt at kell vesszeleniuk. Ha valaki
  // elojelesse tenne oket (vagy "now > last + koz" alakra irna at), a
  // korbefordulas utan a proba OROKRE leallna: az eszkoz sosem lepne ki
  // korabban egyetlen varakozasbol sem, es ez a hiba csak 50 nap uzem utan
  // jelentkezne. Ezert van rola sajat teszt.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const uint32_t base = 0xFFFFFF00u;   // 256 ms-re a korbefordulas elott
  uint32_t last = base;
  uint32_t firedAt = 0;
  for (uint32_t d = 1000; d <= 180000 && firedAt == 0; d += 1000) {
    const uint32_t now = base + d;     // ez SZANDEKOSAN atfordul
    const uint32_t before = last;
    onlineProbeDue(last, now);
    if (last != before) firedAt = d;   // az ora frissult -> tenyleg futott proba
  }
  CHECK(firedAt > 0, "a proba a korbefordulas utan is lefut, nem all le orokre");
  CHECK(firedAt >= 55000 && firedAt <= 65000,
        "es pontosan a ~60 mp-es utem szerint (nem azonnal, nem kesobb)");
}

static void scOP5() {
  // A RESET_DELAY korai lezarasa a router reset utan - es kozben NEM bontjuk
  // le a kapcsolatot, amit epp az imenti proba igazolt.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;       // nincs internet -> eszkalacio
  setup();
  int guard = 0;
  try { while (++guard < 400000 && logIndex(RELAY_LOW + 0) >= 0
               && logIndex(RELAY_HIGH) < 0) loop(); } catch (DeepSleepSignal&) {}
  // a reset pulzus lefutott; mostantol a router es az internet is jo
  g_httpCode = 200; g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  const uint32_t t0 = g_millis;
  guard = 0;
  try { while (++guard < 400000 && !serialHas("RESET_DELAY korai vege")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("RESET_DELAY korai vege"), "a RESET_DELAY korabban veget ert");
  CHECK(g_millis - t0 < 8u * 60 * 1000, "nem a teljes 10 percet varta ki");
  CHECK(!serialHas("Reconnect WIFI in FAILURE_STATE"),
        "a korai agon NINCS disconnect+reconnect - a kapcsolat mar igazolt");
}

// ===========================================================================
// UJ VISELKEDES: LED jelzesek
// ===========================================================================

static void scLEDap() {
  // AP beallito mod: a Wi-Fi LED villog, a statusz LED VEGIG vilagit.
  coldBoot(false, "", "", "", "");
  try { setup(); } catch (DeepSleepSignal&) {}
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk");
  int wifiValtas = 0, ledValtas = 0;
  int lastW = g_pinState[PIN_WIFILED], lastL = g_pinState[PIN_LED];
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 5000) {
      loop();
      if (g_pinState[PIN_WIFILED] != lastW) { wifiValtas++; lastW = g_pinState[PIN_WIFILED]; }
      if (g_pinState[PIN_LED] != lastL)     { ledValtas++;  lastL = g_pinState[PIN_LED]; }
    }
  } catch (DeepSleepSignal&) {}
  CHECK(wifiValtas >= 8 && wifiValtas <= 12, "a Wi-Fi LED 1 Hz-cel villog (5 mp alatt ~10 valtas)");
  CHECK(ledValtas == 0, "a statusz LED nem villog");
  CHECK(g_pinState[PIN_LED] == HIGH, "hanem vegig vilagit");
}

static void scLEDpulse() {
  // A villogas UTEME es a HATARA. A pulzus egy blokkolo belso ciklusban telik,
  // de itt mi magunk hajtjuk a reset_device()-t, tehat kozvetlenul tudunk
  // mintavetelezni. (A stub yield()-je NEM hivja a g_onDelay horgot - ezen
  // bukott el a teszt elso valtozata.)
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  int guard = 0, ledValtas = 0, wifiMagas = 0, lastL = -2;
  const uint32_t t0 = g_millis;
  try {
    while (!reset_device() && ++guard < 200000) {
      const int l = g_pinState[PIN_LED];
      if (lastL != -2 && l != lastL) ledValtas++;
      lastL = l;
      if (g_pinState[PIN_WIFILED] == HIGH) wifiMagas++;
      feedLoopWDT();
      delay(10);
    }
  } catch (DeepSleepSignal&) {}
  const uint32_t pulzus = g_millis - t0;
  CHECK(pulzus >= 89u*1000 && pulzus <= 91u*1000, "a pulzus 90 masodperc");
  // 90 mp / 250 ms felperiodus = ~360 valtas. Tag hatarok, hogy a mintaveteli
  // szemcsezettseg ne tegye torekenne - a lenyeg, hogy 2 Hz, nem 5 vagy 1.
  const int varhato = (int)(pulzus / 250);
  CHECK(ledValtas >= varhato - 5 && ledValtas <= varhato + 5,
        "a statusz LED 2 Hz-cel villog vegig a pulzus alatt");
  CHECK(wifiMagas == 0, "a Wi-Fi LED egyszer sem gyulladt ki - nincs is halozat");
  CHECK(g_pinState[PIN_LED] == HIGH, "a pulzus utan a statusz LED folyamatosra all vissza");
  CHECK(g_pinState[PIN_RELAY] == LOW, "es a router visszakapta az aramot");
}

// ===========================================================================
// UJ VISELKEDES: watchdog a setup() elejen, 1-alapu sorszamok, csendes siker
// ===========================================================================

static void scWDT_EARLY() {
  // A watchdog mar a LittleFS csatolasa ELOTT elesedik. A soros kimenet
  // sorrendje ezt bizonyitja.
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const int wdt = serialIndex("Watchdog enabled");
  const int fs  = serialIndex("Init LittleFS.");
  CHECK(wdt >= 0, "a watchdog elesedett");
  CHECK(fs >= 0 && wdt < fs, "MEG a LittleFS csatolasa elott");
  CHECK(watchdogEnabled, "es tenyleg fel is iratkozott a loop task");
}

static void scSERidx() {
  // A teszt sorszam 1-alapu a soros porton, a belso cycleIndex viszont
  // 0-alapu marad (ahhoz kotodik a vegpontvalaszto es a reset kuszob).
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  int guard = 0;
  try { while (++guard < 400000 && !serialHas("Teszt ciklus index = 2")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Teszt ciklus index = 1"), "az elso teszt sorszama 1, nem 0");
  CHECK(!serialHas("Teszt ciklus index = 0"), "0-dik teszt SEHOL nem jelenik meg");
  CHECK(serialHas("Test failed. | Hibák száma = 1 / 5"),
        "a hibaszamlalo a teszt UTAN, mar novelve jelenik meg");
  CHECK(serialIndex("Teszt ciklus index = 1") < serialIndex("Test failed."),
        "a sorszam a teszt ELOTT, a hibaszam a teszt UTAN all");
}

static void scSERquiet() {
  // Sikeres teszt: se "Igaz érték!", se a kapott torzs - eleg a
  // "Successful Test". Eltereskor viszont MINDKETTO kell.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = 200; g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  setup();
  int guard = 0;
  try { while (++guard < 100000 && !serialHas("Successful Test")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Successful Test"), "a siker kiirodik");
  CHECK(!serialHas("Igaz érték!"), "de az 'Igaz érték!' mar nem");
  CHECK(!serialHas("Microsoft Connect Test"),
        "es a kapott torzset sem irjuk ki sikernel");

  // Most a masik ag: 200-as valasz, de ROSSZ torzs.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = 200; g_httpBody = "captive portal login"; pingSim.ok = true;
  setup();
  guard = 0;
  try { while (++guard < 100000 && !serialHas("Hamis érték!")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Hamis érték!"), "elteresnel megmarad a jelzes");
  CHECK(serialHas("captive portal login"),
        "es a KAPOTT torzs is - abbol derul ki, mi ult a keresre");
}

static void scEVT1() {
  // A /log oldal TEST FAIL parametere ugyanazt a szamot mutatja, mint a
  // soros port "Teszt ciklus index" sora: 1-alapon.
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  int guard = 0;
  try { while (++guard < 400000 && !serialHas("Test failed.")) loop(); }
  catch (DeepSleepSignal&) {}
  bool megvan = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    const EventEntry& e = rtcEvents[i % 32];
    if (e.code == EV_TEST_FAIL_C) { megvan = (e.param == 1); }
  }
  CHECK(megvan, "az elso bukas EV_TEST_FAIL parametere 1, nem 0");
}

struct Scenario { const char* name; void (*fn)(); };
static const Scenario kScenarios[] = {
  { "W1: nincs mentett SSID -> AP konfigurációs portál, NEM alszik el", sc0 },
  { "W2: sikeres csatlakozás -> monitor mód, DHCP (nincs statikus IP)", sc1 },
  { "W3: statikus IP -> config() DNS-sel, a mode() után", sc2 },
  { "W4: hibás IP formátum -> DHCP-re esik vissza, nem konfigurál", sc3 },
  { "W5: nem sikerül csatlakozni -> ~20s timeout, majd újrapróbálkozás", sc4 },
  { "W6: initWiFi() nem bontja le a már élő kapcsolatot", sc5 },
  { "W7: router reset utáni újracsatlakozás megőrzi a statikus IP+DNS-t", sc6 },
  { "W8: reconnectWifi() 3 próba, 30 mp szünetekkel, alvás NÉLKÜL", sc7 },
  { "W9: reconnectWifi() a 2. próbálkozásra sikerül", sc8 },
  { "S1: beragadt gomb induláskor -> 60s alvás, NEM 1 óra", sc9 },
  { "S2: alvás előtt minden kimenet biztonságos állapotba kerül", sc10 },
  { "S4: a gomb-ébresztés csak RTC-képes lábon működik (C3: GPIO0-5)", sc11 },
  { "S3: ébredés = teljes újraindulás, a számlálók nullázódnak", sc12 },
  { "S5: ébredés után a relé holdja rendezetten oldódik fel", scS5 },
  { "S6: hibás hold rögzítés nem akadályozza az alvást és nem néma", scS6 },
  { "RL1: a reset pulzus tényleg 90 másodperc (regresszió a fő hibára)", sc13 },
  { "RL2: az 5. reset esemény deep sleepet vált ki", sc14 },
  { "H1: a záró CR/LF nem buktatja el az egyezést", sc15 },
  { "H2: eltérő tartalom és hibás státusz elbukik", sc16 },
  { "H3: captive portal nagy válaszát el sem olvassa", sc17 },
  { "H4: Content-Length nélküli válasz is korlátozva olvasódik", sc18 },
  { "PG1: 2 sikeres ping után korán kilép", sc19 },
  { "PG2: csupa sikertelen ping - a 3. után eldől", sc20 },
  { "C1: konfig írás/olvasás oda-vissza, csonkítással", sc21 },
  { "B1: egyetlen zajtüske nem indítja újra az eszközt", sc22 },
  { "F1: hiányzó wifimanager.html esetén is van beállító űrlap", sc23 },
  { "WDT1: a watchdog tényleg újraindít, nem csak figyelmeztet", scWDT1 },
  { "WDT2: a 90 mp-es relé pulzus alatt is etetve van", scWDT2 },
  { "WDT3: a ~100 mp-es újracsatlakozás alatt is etetve van", scWDT3 },
  { "WDT4: a hosszú várakozás delay()-jel megy, nem CPU-pörgetéssel", scWDT4 },
  { "WDT5: a watchdog bekapcsolása előtt nem etetünk", scWDT5 },
  { "SN1: ha a gomb-ébresztés armolása hibázik, időzítő a biztonsági háló", scSN1 },
  { "SN2: sikeres armolásnál nincs időzítő", scSN2 },
  { "FS1: nem csatolható LittleFS - a portál elindul, a mentés nem hazudik", scFS1 },
  { "FS2: írásra nem nyitható fájlrendszer", scFS2 },
  { "FS3: megtelt fájlrendszer - rövid írás elkapva", scFS3 },
  { "FS4: visszaolvasásos ellenőrzés kiszúrja a hibás tartalmat", scFS4 },
  { "FS5: törlés tartalék útvonala (csonkolás -> remove)", scFS5 },
  { "FS6: hiányzó fájl olvasása", scFS6 },
  { "FT1: csatolhatatlan LittleFS -> hibajelzés, nem AP portál", scFT1 },
  { "FT2: létező de olvashatatlan konfig -> hibajelzés", scFT2 },
  { "FT3: első indítás (nincs fájl) -> AP portál, NEM hiba", scFT3 },
  { "FT4: wifireset után (üres fájlok) -> AP portál, NEM hiba", scFT4 },
  { "FT5: mindkét LED egyszerre, gyorsan villog", scFT5 },
  { "FT6: hibajelzés közben nem fut az állapotgép, a gombok élnek", scFT6 },
  { "FT7: 5 perc hibajelzés után alvás, időzített ébresztés NÉLKÜL", scFT7 },
  { "FT8: a normál elalvás továbbra is 1 óra múlva ébred", scFT8 },
  { "WF1: mentett SSID + elérhetetlen router -> nem megy azonnal AP módba", scWF1 },
  { "WF2: 10 perc + 3 próba + router reset + 3 próba, majd alvás", scWF2 },
  { "WF3: AP mód 5 perc után alszik, időzített ébresztés nélkül", scWF3 },
  { "WF4: fájlírás közben SOHA nem alszik el", scWF4 },
  { "WF5: minden HTTP kérés kitolja az 5 perces határidőt", scWF5 },
  { "WF6: kapcsolatvesztés -> 3 próba, majd azonnal router reset", scWF6 },
  { "WD1: egy watchdog reset még nem végzetes", scWD1 },
  { "WD2: 3 watchdog reset után végzetes hibajelzés", scWD2 },
  { "WD3: a hibajelzés 5 perc után alszik, csak gombbal ébred", scWD3 },
  { "WD4: áramtalanítás nullázza a watchdog számlálót", scWD4 },
  { "WD5: szoftveres reset nem növel és nem nulláz", scWD5 },
  { "WD6: 1 óra hibátlan működés nullázza a számlálót", scWD6 },
  { "E1: teljes egészséges ciklus (teszt -> SUCCESS -> teszt)", scE1 },
  { "E2: internet kiesik -> router reset -> visszatérés", scE2 },
  { "E3: reset után a WiFi sem jön vissza -> alvás, újrapróbálkozás", scE3 },
  { "E4: startConfigPortal() ismételt hívása nem duplikál", scE4 },
  { "E5: wifireset gomb törli a mentett adatokat", scE5 },
  { "P1: érvényes mentés -> 200, fájlok kiírva, újraindítás", scP1 },
  { "P2: túl hosszú SSID -> 500, nincs újraindulás", scP2 },
  { "P3: hiányzó SSID -> 500", scP3 },
  { "P4: érvénytelen IP -> 500", scP4 },
  { "P5: írásra képtelen FS -> 500", scP5 },
  { "P6: statikus IP mentése, a jelszó nem szivárog a válaszba", scP6 },
  { "P7: a GET oldal kitolja az AP határidőt", scP7 },
  { "CPU1: a loop() a várakozó állapotokban nem pörgeti a CPU-t", scCPU1 },
  { "CPU2: a first start várakozás sem pörget", scCPU2 },
  { "X1: reset gomb a relé pulzus közben -> a router visszakapja az áramot", scX1 },
  { "X2: nyílt hálózat (üres jelszó)", scX2 },
  { "X3: SSID/jelszó pontos határértékei", scX3 },
  { "X4: fél-konfigurált statikus IP -> DHCP", scX4 },
  { "X5: csak whitespace a konfig fájlban", scX5 },
  { "X6: sikertelen mentés is kitolja az AP határidőt", scX6 },
  { "PO1: áramszünet, router 8 perc múlva -> kivárja", scPO1 },
  { "PO2: router 11 perc múlva -> még elkapja", scPO2 },
  { "PO3: router 20 perc múlva -> a reset utáni próbálkozás elkapja", scPO3 },
  { "R1: rossz jelszó -> azonnal AP mód", scR1 },
  { "R2: 33 kör (~2 nap) után AP mód", scR2 },
  { "R3: a 2 napos számítás ellenőrzése (33 x 85,5 perc)", scR3 },
  { "R4: sikeres csatlakozás nullázza a 2 napos ablakot", scR4 },
  { "L1: eseménynapló és a /log oldal", scL1 },
  { "L2: körpuffer túlcsordulás", scL2 },
  { "L3: a végzetes hiba oka naplózva", scL3 },
  { "SB1: beragadt reset gomb -> 60 mp alvás", scSB1 },
  { "SB2: beragadt wifireset gomb -> ugyanaz", scSB2 },
  { "SB3: a LED-ek FELVÁLTVA villognak elalvás előtt", scSB3 },
  { "SB4: a beragadt gomb naplózva", scSB4 },
  { "SB5: az ismétlődő beragadt-gomb kör nem árasztja el a naplót", scSB5 },
  { "SER1: normál működés soros terhelése", scSER1 },
  { "SER2: internet kiesés soros terhelése", scSER2 },
  { "SER3: AP és hibajelző mód néma", scSER3 },
  { "OV1: több reset ciklus, számlálók korlátosak", scOV1 },
  { "WDT6: nem futó TWDT esetén a sketch felhúzza és tényleg feliratkozik", scWDT6 },
  { "WDT7: sikertelen feliratkozásnál nem etet és nem hazudik védelmet", scWDT7 },
  { "WDT8: sikertelen konfigurálásnál sem jelent 90 mp-es védelmet", scWDT8 },
  { "L4: 'nincs mentett SSID' AP mód is bekerül a naplóba", scL4 },
  { "L5: végzetes hiba utáni elalvás is bekerül a naplóba", scL5 },
  { "P8: statikus IP gateway nélkül nem fogadható el sikerként", scP8 },
  { "P9: a hosszú hibaindoklás nem csonkolódik", scP9 },
  { "H5: beragadt szervernél a saját olvasási határidő tart", scH5 },
  { "H6: üres elvárásnál 204-et követel, a captive portált elutasítja", scH6 },
  { "H7: 204-nél a törzs olvasásába bele sem kezd", scH7 },
  { "H8: az eszkaláció 5 különböző célpontot próbál végig", scH8 },
  { "H9: befagyott router-DNS (HTTP néma, ICMP jó) -> router reset", scH9 },
  { "IP1: a stub ugyanúgy viselkedik, mint a core IPAddress-e", scIP1 },
  { "IP2: IPv6 és 0.0.0.0 cím nem fogadható el a portálon", scIP2 },
  { "IP3: mentett IPv6 gateway esetén DHCP, nem csonka statikus konfig", scIP3 },
  { "P10: mentés közben a reset gomb nem indít újra", scP10 },
  { "P11: mentés közben a wifireset gomb nem töröl és nem indít újra", scP11 },
  { "P12: a mentett érték megegyezik azzal, amit az eszköz használni fog", scP12 },
  { "P13: csupa szóközből álló SSID nem fogadható el", scP13 },
  { "LED1: reset pulzus alatt a státusz LED villog, a Wi-Fi LED sötét", scLED1 },
  { "KA1: van /ping végpont, apró válasszal", scKA1 },
  { "KA2: nyitva tartott lap mellett nem alszik el", scKA2 },
  { "KA3: a lap bezárása után az utolsó pingtől számít 5 percet", scKA3 },
  { "KA4: keep-alive mindkét űrlapon és a naplóoldalon", scKA4 },
  { "KA5: az AP jelszó a WPA2 hossztartományban van", scKA5 },
  { "SE1: a jelszó kódolása oda-vissza hibátlan", scSE1 },
  { "SE2: a fájlban nem található a jelszó (strings-ellenálló)", scSE2 },
  { "SE3: ismétlődő karakterek nem adnak ismétlődő bájtokat", scSE3 },
  { "SE4: másik lapkán a kimásolt fájl nem működik", scSE4 },
  { "SE5: a régi, nyílt szöveges mentés továbbra is működik", scSE5 },
  { "SE6: hibás tartalom nem végzetes hiba", scSE6 },
  { "SE7: mentés -> újraindulás -> csatlakozás végponttól végpontig", scSE7 },
  { "SE8: a WiFi.begin() a NYÍLT jelszót kapja, nem a kódoltat", scSE8 },
  { "SE9: kapcsolatvesztés utáni újracsatlakozás is jó jelszóval", scSE9 },
  { "SE10: router reset utáni újracsatlakozás is jó jelszóval", scSE10 },
  { "GW1: elérhető gateway esetén a viselkedés változatlan", scGW1 },
  { "GW2: elérhetetlen gateway -> egy reset, majd AP mód + napló", scGW2 },
  { "GW3: DHCP-nél nincs gateway-ellenőrzés", scGW3 },
  { "P16: a validáció mindkét űrlapon azonos", scP16 },
  { "P17: az IP/gateway mező szóközei vágva validálódnak", scP17 },
  { "P18: csupa szóköz IP/gateway = DHCP, nem hiba", scP18 },
  { "P19: zárolt konfignál a webes mentés 503-mal hátrál", scP19 },
  { "P20: részleges POST a mentett statikus párral konzisztens", scP20 },
  { "WR1: fájlírás közben nem indul újra (halasztott újraindítás)", scWR1 },
  { "WR2: fájlírás közben nem alszik el", scWR2 },
  { "WR3: a várakozás korlátos, beragadt jelző nem fagyaszt le", scWR3 },
  { "MW1: a relé pulzus a millis() körbefordulása alatt is 90 mp", scMW1 },
  { "MW2: körbefordulás a RESET_DELAY alatt és a hibakezelés közepén", scMW2 },
  { "MW3: 169 nap (3 körbefordulás) után változatlan viselkedés", scMW3 },
  { "WD7: HTTP timeoutok mellett is 90 mp alatt marad az etetési köz", scWD7 },
  { "WD8: gateway-ellenőrzéssel együtt is 90 mp alatt", scWD8 },
  { "WD9: AP mód, végzetes hiba, first start - mind 90 mp alatt", scWD9 },
  { "WD10: a watchdog a WiFi.begin() ELŐTT élesedik", scWD10 },
  { "WD11: a LittleFS formázás szándékosan a felügyeleten kívül", scWD11 },
  { "WD12: a setup() hátralévő része sem lép 90 mp fölé", scWD12 },
  { "WD13: halott DNS (33 mp/kérés) mellett is 90 mp alatt marad", scWD13 },
  { "P14: a halasztott újraindítás a türelmi idő UTÁN fut le", scP14 },
  { "L6: minden eseménykód olvasható címkét kap a /log oldalon", scL6 },
  { "L7: üres napló esetén nincs üres táblázat", scL7 },
  { "F2: feltöltött data/ esetén a fájlokat szolgálja ki", scF2 },
  { "F3: ismeretlen útvonal 404, nem szivárog fájltartalom", scF3 },
  { "F4: minden végpont kitolja az AP határidőt", scF4 },
  { "P15: túl hosszú jelszó elutasítva (a maxlength csak a böngészőt köti)", scP15 },
  { "E6: visszatérő WiFi esetén nincs felesleges router reset", scE6 },
  { "X7: a wifireset az SSID-t törli először", scX7 },
  { "X8: sikertelen wifireset törlés végzetes hiba, nem újraindítás", scX8 },
  { "X9: query-string paraméter nem írhatja át a konfigurációt", scX9 },
  { "X10: egy napnál hosszabb uptime helyesen jelenik meg", scX10 },
  { "X11: sikertelen WiFi.config() után is megpróbál csatlakozni", scX11 },
  { "X12: a sikertelen törlés jelzése együtt villogó LED-ek", scX12 },
  { "X13: a reset gomb a hibajelzés alatt is működik", scX13 },
  { "X14: a sikeres törlés továbbra is sima újraindítás", scX14 },
  { "FS7: néma írási hiba - a visszaolvasás fogja meg", scFS7 },
  { "FS8: néma írási hiba a portálon sem jelent sikert", scFS8 },
  { "FS9: csonka olvasás -> végzetes hiba, nem csonka konfig", scFS9 },
  { "FS10: a fileMatches() minden elbukási módja", scFS10 },
  { "R5: a rossz jelszó csak a 2. próbálkozástól látszik (STA.cpp)", scR5 },
  { "R6: egy újrapróbálkozási kör ébren töltött ideje 25,5 perc", scR6 },
  { "R7: a felismerési idő 123 mp élő és 213 mp halott DNS mellett", scR7 },
  { "R8: 33 kör, 32 alvás - a tényleges türelem ~46 óra", scR8 },
  { "CH1: a chunked válasz keretbájtjai nem buktatják el a tesztet", scCH1 },
  { "CH2: több darabra vágott választ is összefűz", scCH2 },
  { "CH3: darab-kiterjesztés és nagybetűs hexa", scCH3 },
  { "CH4: szabálytalan keretezés -> bukás, nem találgatás", scCH4 },
  { "CH5: chunked captive portal sem olvastat végig 50 kB-ot", scCH5 },

  // --- Korai kilepes a hosszu varakozasokbol ---
  { "OP1: a firstStartDelay korán véget ér, ha a hálózat ÉS az internet visszajött", scOP1 },
  { "OP2: ha csak a hálózat jön vissza, a delay szabályosan végigfut", scOP2 },
  { "OP3: ép induláskor az első próba azonnal fut (nem késik egy ütemet)", scOP3 },
  { "OP4: flash kímélés – nincs fájlírás, és percenként max egy WiFi.begin()", scOP4 },
  { "OP5: a RESET_DELAY korán zárul, és nem bontja le az igazolt kapcsolatot", scOP5 },
  { "OP6: a próba órái túlélik a millis() körbefordulását", scOP6 },
  { "OP7: élő Wi-Fi mellett is korán zárul, ha az internet menet közben visszajön", scOP7 },

  // --- Gombok a hosszu varakozasok alatt ---
  { "BTN1: a hosszú várakozások alatt 10 ms-onként nézzük mindkét gombot", scBTN1 },
  { "BTN2: a blokkoló HTTP kérés alatti vak ablak mérése (egy kérésnyi)", scBTN2 },
  { "BTN3: a végig nyomva tartott gomb a vak ablak után is hat", scBTN3 },

  // --- LED jelzesek ---
  { "LED2: AP módban a Wi-Fi LED villog, a státusz LED végig világít", scLEDap },
  { "LED3: a reset pulzus villogása 2 Hz, és pontosan a pulzus alatt tart", scLEDpulse },

  // --- Watchdog, soros kimenet, esemenynaplo ---
  { "WDT7: a watchdog már a LittleFS csatolása ELŐTT élesedik", scWDT_EARLY },
  { "SER4: a teszt sorszáma 1-alapú, a hibaszám a teszt után áll", scSERidx },
  { "SER5: sikernél csak 'Successful Test', eltérésnél a kapott törzs is", scSERquiet },
  { "EVT1: a /log TEST FAIL paramétere is 1-alapú", scEVT1 },
};


// Minden forgatókönyv KÜLÖN PROCESSZBEN fut. A sketch globális állapota
// (testState, timing, uiFlags, ...) így garantáltan friss minden esetben -
// pontosan úgy, mint egy valódi hidegindításnál.
struct Result { int checks, failures; };

static Result runIsolated(const Scenario& sc) {
  int fds[2];
  if (pipe(fds) != 0) { perror("pipe"); exit(2); }
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    checks = failures = 0;
    printf("\n\033[1m%s\033[0m\n", sc.name);
    try { sc.fn(); }
    catch (RestartSignal&)   { printf("  \033[31mFAIL\033[0m váratlan ESP.restart()\n"); failures++; checks++; }
    catch (DeepSleepSignal&) { printf("  \033[31mFAIL\033[0m váratlan deep sleep\n");    failures++; checks++; }
    Result r{checks, failures};
    ssize_t n = write(fds[1], &r, sizeof(r));
    (void)n;
    close(fds[1]);
    fflush(stdout);
#ifdef COVERAGE_BUILD
    // A gyerekprocessz _exit()-tel lep ki, ami NEM uriti a gcov szamlalokat.
    // Lefedettseg-meresnel tehat kezzel kell kiirni oket.
    __gcov_dump();
#endif
    _exit(0);
  }
  close(fds[1]);
  Result r{0, 0};
  if (read(fds[0], &r, sizeof(r)) != (ssize_t)sizeof(r)) {
    printf("  \033[31mFAIL\033[0m a forgatókönyv összeomlott\n");
    r.checks = 1; r.failures = 1;
  }
  close(fds[0]);
  int st = 0; waitpid(pid, &st, 0);
  return r;
}

int main(int argc, char** argv) {
  const char* only = (argc > 1) ? argv[1] : nullptr;
  int totalChecks = 0, totalFailures = 0, ran = 0;
  for (const Scenario& sc : kScenarios) {
    if (only && strncmp(sc.name, only, strlen(only)) != 0) continue;
    Result r = runIsolated(sc);
    totalChecks += r.checks; totalFailures += r.failures; ran++;
  }
  printf("\n\033[1m%d/%d ellenőrzés sikeres (%d forgatókönyv)\033[0m\n",
         totalChecks - totalFailures, totalChecks, ran);
  return totalFailures ? 1 : 0;
}
