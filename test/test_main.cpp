// Host tesztek: a valódi sketch forgatókönyvei stub API-k felett, forgatókönyvenként külön processzben.
#include <Arduino.h>
#include <set>
#include <algorithm>
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
#include <cstdlib>
#ifdef COVERAGE_BUILD
extern "C" void __gcov_dump(void);
#endif

void setup(); void loop();

static void (*const sketchLoop)() = &loop;
static void (*const sketchSetup)() = &setup;

struct ProgramScope {
  ProgramScope()  { g_wdtInProgram = true;  }
  ~ProgramScope() { g_wdtInProgram = false; }
};

static void coreLoopStep() {
  feedLoopWDT_fromCore();
  ProgramScope sc;
  sketchLoop();
}
static void coreSetupStep() {
  ProgramScope sc;
  sketchSetup();
}
#define loop() coreLoopStep()
#define setup() coreSetupStep()
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
bool filesystemReady();
void setFilesystemReady(bool ready);
extern uint32_t rtcWdtMagic;
extern uint32_t rtcWdtResets;

extern volatile uint32_t apDeadline;
extern uint32_t rtcRetryRounds;
extern uint32_t rtcEvMagic;
extern uint32_t rtcEvNext;
enum EventCode : uint8_t;
extern uint32_t rtcCarryRetryRounds;

extern int g_randomCalls;
extern uint32_t g_randomState;
extern uint64_t g_efuseMac;
bool gatewayPingOk();
void ensureFallbackDns();
int lastHttpCode();
uint16_t httpCodeLogParam(int code);
bool staticPairValid(const IPAddress& ip, const IPAddress& gateway);
ConfigStatus readSecretValue(fs::FS& fs, const char* path, char* out, size_t outSize);

struct EventEntry { uint32_t uptimeSec; uint32_t epoch; uint16_t param;
                    uint8_t code; uint8_t pad; };
static_assert(sizeof(EventEntry) == 12, "az EventEntry merete a fajlformatum resze");
extern EventEntry rtcEvents[];

struct TestState { uint8_t endpointIndex = 0; uint8_t resetEvents = 0;
                   uint8_t resetStep = 0; bool outage = false;
                   uint32_t outageStart = 0; uint32_t lastResetAt = 0; };
struct TimingState { uint32_t stateStart = 0; uint32_t resetPulseStart = 0;
                     uint32_t startMillis = 0; uint32_t resetBtnDownSince = 0;
                     uint32_t wifiResetBtnDownSince = 0; uint32_t blinkLast = 0;
                     uint32_t fatalStart = 0; };
struct UIFlags { bool firstStartPrinted = false; bool firstStart = true;
                 bool blinkOn = false; };
extern TestState testState;
extern TimingState timing;
extern UIFlags uiFlags;
extern bool staticConfigActive;
extern bool watchdogEnabled;
extern volatile bool btnResetLatched, btnWifiResetLatched;
extern volatile uint32_t btnResetDownAt, btnWifiResetDownAt;
extern int g_criticalMaxDepth;
extern int g_criticalEnters;

void requestRestart(uint32_t delayMs);
bool restartRequestDue(uint32_t now);
bool restartRequested();
void clearRestartRequest();
bool configWriteInProgress();
bool beginConfigWrite();
void endConfigWrite();
extern uint32_t resetDelayProbeLast;
extern uint32_t firstStartProbeLast;
bool onlineProbeDue(uint32_t& lastProbe, uint32_t now);
void logEvent(EventCode code, uint16_t param);
bool lastEventWas(uint8_t code);
bool stuckCycleAlreadyLogged(uint16_t which);
void lockConfigBeforeShutdown();
extern uint32_t g_epochNow;
void doWifiReset();
extern int g_ntpStarts;
void enterFatal(const char* reason);
void startConfigPortal();
bool beginConfigWrite();
void checkHeap(uint32_t now);
extern uint32_t heapCheckLast;
extern uint32_t heapLogLast;
extern uint8_t  heapCritStreak;
extern bool     heapWarnActive;

void ntpResetForColdBoot();
constexpr uint8_t EVLOG_SIZE_C = 32;
constexpr size_t  SSID_MAX_LEN_C = 32;
constexpr size_t  IPSTR_MAX_LEN_C = 15;
constexpr uint16_t EVFILE_SIZE_C = 128;

struct EvFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t evNextAtSave;
  uint32_t savedEpoch;
  uint32_t savedUptime;
};
static_assert(sizeof(EvFileHeader) == 20, "az EvFileHeader merete a fajlformatum resze");
const char* eventName(uint8_t code);
bool saveEventLog(const char* reason);
bool loadEventLogHeader(EvFileHeader& fej);
bool loadEventLogRange(const EvFileHeader& fej, uint32_t tol, uint16_t db, EventEntry* ki);
void printEventLogs();
void checkWatchdogResets();
bool gatewayUnreachable();
extern std::set<std::string> g_fsDirs;
extern std::string g_serialIn;
extern bool g_serialReadLies;
constexpr uint8_t EV_TEST_FAIL_C = 4;
constexpr uint8_t EV_FATAL_C = 9;
void resetbutton();
void wifiresetbutton();
void restartFromButton(const char* reason);
void enterDeepSleep(uint64_t timerUs);
void waitWithButtons(uint32_t);
void touchApDeadline();
void printUptime();
void decodeSecretInPlace(char* buf);
bool encodeSecret(const char* plain, char* out, size_t outSize);
void startConfigPortal();

static const int PIN_RELAY   = 10;
static const int PIN_LED     = 6;
static const int PIN_WIFILED = 5;
static const int PIN_RESETBTN = 3;
static const int PIN_WIFIBTN  = 2;
#define RELAY_HIGH "pin10=HIGH"
#define RELAY_LOW  "pin10=LOW"
#define LED_HIGH   "pin6=HIGH"
#define LED_LOW    "pin6=LOW"

static int failures = 0, checks = 0;

static constexpr uint32_t WDT_FEED_GAP_LIMIT_MS = 45000;

static const char* g_wdtGapWaiver = nullptr;

#define CHECK(cond, msg) do { checks++; if(!(cond)) { printf("  \033[31mFAIL\033[0m %s\n", msg); failures++; } \
                              else printf("  ok   %s\n", msg); } while(0)

static bool serialHas(const char* frag) {
  for (auto& l : g_serialLog) if (l.find(frag) != std::string::npos) return true;
  return false;
}

static int serialIndex(const char* frag) {
  for (size_t i = 0; i < g_serialLog.size(); i++)
    if (g_serialLog[i].find(frag) != std::string::npos) return (int)i;
  return -1;
}

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

static void coldBoot(bool willConnect, const char* s, const char* p,
                     const char* ip, const char* gw, uint32_t latency = 500,
                     bool deepSleepWake = false) {
  g_millis = 1; g_log.clear(); g_serialLog.clear(); g_pinState.clear(); g_pinRead.clear();

  g_wdtLastFeed = 0; g_wdtMaxFeedGap = 0;

  g_relayMaxHighMs = 0; g_relayHighSince = 0; g_relayIsHigh = false;
  g_criticalMaxDepth = 0;

  g_freeHeap = 180000; g_minFreeHeap = 180000; g_maxAllocHeap = 110000;
  g_heapDrainPerCall = 0; g_heapQueries = 0;

  heapCheckLast = 0; heapLogLast = 0; heapCritStreak = 0; heapWarnActive = false;
  ntpResetForColdBoot();
  g_epochNow = 0; g_ntpStarts = 0;

  g_serialOn = false; g_serialBaud = 0; g_serialFirstWriteMs = 0;
  g_serialWritesAfterEnd = 0; g_serialFlushedAll = true;
  g_fs.clear(); g_fsMountOk = true; g_fsMountMs = 0; g_wakeupUs = 0;
  g_fsWritable = true; g_fsCapacity = 0; g_fsRemoveOk = true; g_fsReadable = true;
  g_fsShortReadSkip = 0;
  g_resetReason = deepSleepWake ? ESP_RST_DEEPSLEEP : ESP_RST_POWERON;
  if (!deepSleepWake) {
    rtcRetryRounds = 0;
  }

  clearRestartRequest(); endConfigWrite();

  testState = TestState();
  timing = TimingState();
  uiFlags = UIFlags();

  resetDelayProbeLast = 0;
  firstStartProbeLast = 0;
  staticConfigActive = false;
  watchdogEnabled = false;
  g_gpioWakeResult = 0;
  g_gpioWakeMask = 0; g_gpioWakeMode = -1;
  g_isr.clear(); g_isrMode.clear();
  rtcCarryRetryRounds = 0;
  g_randomCalls = 0;
  btnResetLatched = false; btnWifiResetLatched = false;
  btnResetDownAt = 0; btnWifiResetDownAt = 0;

  g_gpioHoldFails = false;
  if (!deepSleepWake) { g_heldPins.clear(); g_deepSleepHoldEnabled = false; }
  g_httpCode = 200; g_httpSize = -2; g_httpBeginOk = true; g_httpBody = "Microsoft NCSI";
  g_httpChunked = false; g_httpChunkSize = 0; g_httpRawOverride.clear();
  g_httpAutoOk = false;
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

static const uint8_t EVC_TEST_FAIL = 4, EVC_HTTP_ERROR = 15, EVC_NET_DIAG = 16;

static int evCount(uint8_t code, int param = -1) {
  const uint32_t db = (rtcEvNext < 32) ? rtcEvNext : 32;
  int n = 0;
  for (uint32_t k = rtcEvNext - db; k < rtcEvNext; k++) {
    const EventEntry& e = rtcEvents[k % 32];
    if (e.code == code && (param < 0 || e.param == (uint16_t)param)) n++;
  }
  return n;
}

static int relayPulses() {
  int n = 0;
  for (auto& l : g_log) if (l == RELAY_HIGH) n++;
  return n;
}

template <class F>
static int futtat(uint32_t maxMs, F kesz) {
  const uint32_t t0 = g_millis;
  int guard = 0;
  try {
    while (g_millis - t0 < maxMs && ++guard < 3000000 && !kesz()) loop();
  } catch (DeepSleepSignal&) { return 1; }
  catch (RestartSignal&) { return 2; }
  return 0;
}

void retrySleep();

// Egy router reset pulzus, majd a Wi-Fi nelkuli ujraprobalkozas alvasa; a visszateres az ebresztesi ido.
static uint64_t egyResetUtanAlvas() {
  int guard = 0;
  while (!reset_device() && ++guard < 200000) { yield(); }
  try { retrySleep(); } catch (DeepSleepSignal& d) { return d.us; }
  return UINT64_MAX;
}

static void ujNaplo() { rtcEvMagic = 0; rtcEvNext = 0; }

static void sc0() {
  coldBoot(false, "", "", "", "");
  try { setup(); CHECK(true, "setup() lefutott deep sleep nélkül"); }
  catch (DeepSleepSignal&) { CHECK(false, "setup() NEM aludhat el SSID hiányában"); }
  CHECK(deviceMode == (DeviceMode)1, "MODE_CONFIG lett");
  CHECK(wifiSim.softApCount == 1, "softAP() elindult");
  CHECK(wifiSim.beginCount == 0, "WiFi.begin() nem futott (nincs SSID)");
  {
    bool slept = false;
    const uint32_t t0 = g_millis;
    try { for (int i = 0; i < 200000 && g_millis - t0 < 4u*60*1000; i++) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    catch (RestartSignal&) {}

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

    bool ok = false;

    uint32_t releaseAt = g_millis + 25000;

    (void)releaseAt;
    wifiSim.willConnect = false;

    try {
      ok = initWiFi();
      CHECK(!ok, "1. kísérlet elbukott");
      wifiSim.willConnect = true;
      ok = initWiFi();
      CHECK(ok, "2. kísérlet sikerült, miután visszajött a hálózat");
    } catch (DeepSleepSignal&) { CHECK(false, "nem lett volna szabad elaludnia"); } }
}

static void sc9() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[3] = LOW;
  { bool slept = false; uint64_t us = 0;
    try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
    CHECK(slept, "beragadt gombnál elalszik");
    CHECK(us == 60ULL*1000000ULL, "60 másodperces ébresztés");
    CHECK(g_gpioWakeMask == 0, "gomb-ébresztés NINCS armolva (különben boot loop)"); }
}

static void sc10() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  {
    egyResetUtanAlvas();
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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  egyResetUtanAlvas();
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
        "ébredés után ellenőrzés / MONITOR (RTC memóriát nem használunk)");
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
}

static void sc22() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  { bool restarted = false;
    g_pinRead[3] = LOW;
    try { resetbutton(); }
    catch (RestartSignal&) { restarted = true; }
    CHECK(!restarted, "az első LOW mintavétel nem indít újra");
    g_pinRead[3] = HIGH;
    try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
    CHECK(!restarted, "felengedés után sem");

    g_pinRead[3] = LOW;
    try { resetbutton(); g_millis += 100; resetbutton(); }
    catch (RestartSignal&) { restarted = true; }
    CHECK(restarted, "tartós (>50ms) nyomásra viszont újraindul"); }
}

static void sc23() {
  coldBoot(false, "", "", "", "");
  g_fs.clear();
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

  CHECK(g_handlers.count("/style.css#1") == 0, "nincs /style.css útvonal");
  CHECK(g_handlers.count("/favicon.png#1") == 0, "nincs /favicon.png útvonal");

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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  wifiSim.willConnect = false; wifiSim.begun = false;
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  try { reconnectWifi(); } catch (DeepSleepSignal&) {}
  CHECK(g_wdtMaxFeedGap < g_wdtTimeoutMs, "a ~100 mp-es újracsatlakozás alatt is etetve volt");
  CHECK(g_wdtMaxFeedGap <= 50, "etetési köz ~10 ms");
}

static void scWDT4() {
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
  coldBoot(false, "", "", "", "");
  g_fsMountOk = false;
  try { setup(); } catch (DeepSleepSignal&) { CHECK(false, "nem szabadna elaludnia"); }
  CHECK(!filesystemReady(), "a fajlrendszer nem csatolhato");
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL - mentés úgysem lenne lehetséges");
  CHECK(g_wdtEnabled, "a watchdog hibajelzés közben is aktív");
}

static void scFS2() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fsWritable = false;
  CHECK(!writeConfigValue(LittleFS, "/x.txt", "adat"), "sikertelen megnyitás -> false");
  g_fsWritable = true;
  CHECK(writeConfigValue(LittleFS, "/x.txt", "adat"), "működő FS-en -> true");
}

static void scFS3() {
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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(writeConfigValue(LittleFS, "/v.txt", "helyes"), "normál írás rendben");
  g_fs["/v.txt"] = "serult";
  CHECK(!fileMatches(LittleFS, "/v.txt", "helyes", 6), "az ellenőrzés kiszúrja az eltérést");
}

static void scFS5() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fs["/c.txt"] = "valami";
  g_fsWritable = false;
  g_fsRemoveOk = true;
  CHECK(clearConfigValue(LittleFS, "/c.txt"), "a remove() tartalékra vált");
  CHECK(!g_fs.count("/c.txt"), "a fájl tényleg eltűnt");

  g_fs["/d.txt"] = "valami";
  g_fsRemoveOk = false;
  CHECK(!clearConfigValue(LittleFS, "/d.txt"), "ha egyik sem megy, false-t ad");
  g_fsWritable = true; g_fsRemoveOk = true;
}

static void scFS6() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  char buf[33];
  memset(buf, 'X', sizeof(buf));
  CHECK(readConfigValue(LittleFS, "/nincs_ilyen.txt", buf, sizeof(buf)) == (ConfigStatus)1, "hiányzó fájl -> CONFIG_MISSING");
  CHECK(buf[0] == '\0', "a buffer akkor is ki lett ürítve");
}

static void scFT1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  CHECK(wifiSim.softApCount == 0, "NEM indult AP portál");
  CHECK(wifiSim.beginCount == 0, "meg sem próbált csatlakozni");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scFT2() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsReadable = false;
  setup();
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  CHECK(wifiSim.softApCount == 0, "NEM indult AP portál");
}

static void scFT3() {
  coldBoot(false, "", "", "", "");
  g_fs.clear();
  setup();
  CHECK(deviceMode == (DeviceMode)1, "MODE_CONFIG - AP portál, nem hibajelzés");
  CHECK(wifiSim.softApCount == 1, "elindult az AP");
}

static void scFT4() {
  coldBoot(false, "", "", "", "");
  g_fs["/ssid.txt"] = ""; g_fs["/pass.txt"] = "";
  g_fs["/ip.txt"] = "";   g_fs["/gateway.txt"] = "";
  setup();
  CHECK(deviceMode == (DeviceMode)1, "MODE_CONFIG - AP portál, nem hibajelzés");
  CHECK(wifiSim.softApCount == 1, "elindult az AP");
}

static void scFT5() {
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
      if (toggles > 2) break;
    }
  }
  CHECK(toggles >= 2, "villog (1 mp alatt többször váltott)");

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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const uint64_t us = egyResetUtanAlvas();
  CHECK(us == 3600ULL * 1000000ULL, "1 órás ébresztés megmaradt a router reset utáni útvonalon");
}

static void scWF1() {
  coldBoot(false, "MyNetwork", "titok123", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "MONITOR mód - kivárja a first start delayt");
  CHECK(wifiSim.softApCount == 0, "nem indult AP portál");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scWF2() {
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

  CHECK(begins <= 6 + (int)(dt / 60000) + 2,
        "a proba percenkent legfeljebb egy begin()-t ad hozza");
  CHECK(logIndex(RELAY_HIGH) >= 0, "a körben lefutott egy router újraindítás");
  CHECK(dt >= 10u*60*1000, "megvárta a 10 perces first start delayt");
  CHECK(dt >= 25u*60*1000, "és a reset + bootolási várakozást is (~25,5 perc)");
  CHECK(dt < 27u*60*1000, "de nem sokkal többet");
}

static void scWF3() {
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
  coldBoot(false, "", "", "", "");
  setup();
  (void)beginConfigWrite();
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 8u*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "mentés közben NEM aludt el, pedig letelt az 5 perc");
  endConfigWrite();
  try { loop(); } catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "a mentés befejeztével viszont elalszik");
}

static void scWF5() {
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
  coldBoot(true, "MyNetwork", "pw", "", "");
  setup();
  loop();
  CHECK(deviceMode == (DeviceMode)0, "monitor módban vagyunk");
  const int beginBefore = wifiSim.beginCount;
  wifiSim.willConnect = false; wifiSim.begun = false;
  g_log.clear();
  int guard = 0;
  try { while (logIndex(RELAY_HIGH) < 0 && ++guard < 200000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(wifiSim.beginCount - beginBefore == 3, "3 újrapróbálkozás a reset előtt");
  CHECK(logIndex(RELAY_HIGH) >= 0, "elindult a router újraindítás (relé be)");
}

static void scWD1() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0; rtcWdtResets = 0;
  g_resetReason = ESP_RST_TASK_WDT;
  setup();
  CHECK(rtcWdtResets == 1, "első watchdog reset elkönyvelve");
  CHECK(deviceMode == (DeviceMode)0, "normálisan fut tovább");
}

static void scWD2() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0; rtcWdtResets = 0;
  g_resetReason = ESP_RST_TASK_WDT;
  setup();
  CHECK(rtcWdtResets == 1, "1. reset");

  coldBoot(true, "TestNet", "pw", "", "");
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
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0; rtcWdtResets = 2;
  g_resetReason = ESP_RST_TASK_WDT;
  rtcWdtMagic = 0x42415A53UL;
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
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0x42415A53UL; rtcWdtResets = 2;
  g_resetReason = ESP_RST_SW;
  setup();
  CHECK(rtcWdtResets == 2, "szoftveres reset nem növel és nem nulláz");
  CHECK(deviceMode == (DeviceMode)0, "normálisan fut");
}

static void scWD6() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcWdtMagic = 0x42415A53UL; rtcWdtResets = 2;
  g_resetReason = ESP_RST_SW;
  g_httpBody = "Microsoft Connect Test";
  setup();
  CHECK(rtcWdtResets == 2, "két korábbi reset még számon van tartva");

  loop();
  g_millis = 61u * 60 * 1000;
  loop();
  CHECK(rtcWdtResets == 0, "1 óra hibátlan működés után nullázódott");
}

static void scE1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  loop();
  CHECK(deviceMode == (DeviceMode)0, "monitor mód");
  loop();
  CHECK(currentState == (State)1, "sikeres ellenőrzés -> várakozás");
  CHECK(g_pinState[5] == HIGH, "wifi LED világít");
  const uint32_t korKezdete = timing.stateStart;
  int guard = 0;
  while (currentState == (State)1 && ++guard < 100000) { loop(); g_millis += 10; }
  CHECK(g_millis - korKezdete >= 60000, "a következő ellenőrzés 1 perccel az előző kezdete után");
  CHECK(g_millis - korKezdete < 60100, "és nem később");
}

static void scE2() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Valami mas";
  pingSim.ok = false;
  setup();
  loop();
  g_log.clear();
  int guard = 0;
  while (logIndex(RELAY_HIGH) < 0 && ++guard < 300000) { loop(); g_millis += 10; }
  CHECK(guard < 300000, "eljutott a router resetig");
  const int relayOn = logIndex(RELAY_HIGH);
  const int relayOff = logIndex(RELAY_LOW);
  CHECK(relayOn >= 0 && relayOff > relayOn, "relé be, majd ki");

  guard = 0;
  while (currentState != (State)0 && ++guard < 300000) { loop(); g_millis += 10; }
  CHECK(guard < 300000, "visszatért az ellenőrzéshez a reset után");
  CHECK(deviceMode == (DeviceMode)0, "monitor módban maradt (a WiFi jó volt)");
}

static void scE3() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  loop();
  CHECK(deviceMode == (DeviceMode)0, "monitor mód");
  wifiSim.willConnect = false; wifiSim.begun = false;
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
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(wifiSim.softApCount == 1, "egyszer indult");
  startConfigPortal();
  startConfigPortal();
  CHECK(wifiSim.softApCount == 1, "ismételt hívás nem indít újabb AP-t");
}

static void scE5() {
  coldBoot(false, "RegiHalozat", "regijelszo", "", "");
  setup();
  g_fs["/ssid.txt"] = "RegiHalozat";
  bool restarted = false;
  g_pinRead[2] = LOW;
  try { loop(); g_millis += 100; loop(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a wifireset gomb újraindított");
  CHECK(g_fs["/ssid.txt"].empty() || !g_fs.count("/ssid.txt"),
        "a mentett SSID törölve lett");
}

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
  CHECK(restartRequested(), "újraindítás beütemezve");
  CHECK(!configWriteInProgress(), "a mentés jelző visszaállt");
}

static void scP2() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const char* longSsid = "012345678901234567890123456789012345";
  const int code = postConfig(longSsid, "jelszo", "", "", &body);
  CHECK(code == 500, "HTTP 500 - nem hazudik sikert");
  CHECK(!restartRequested(), "NEM indul újra, az adatok nem vesznek el");
  CHECK(body.find("SSID") != std::string::npos, "a válasz megnevezi az okot");
}

static void scP3() {
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig(nullptr, "jelszo", "", "");
  CHECK(code == 500, "HTTP 500 hiányzó SSID esetén");
  CHECK(!restartRequested(), "nem indul újra");
}

static void scP4() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("MyNetwork", "jelszo", "nem-ip-cim", "", &body);
  CHECK(code == 500, "HTTP 500 rossz IP esetén");
  CHECK(!restartRequested(), "nem indul újra");
  CHECK(body.find("IP") != std::string::npos, "a válasz megnevezi az okot");
  CHECK(!g_fs.count("/ssid.txt") && !g_fs.count("/pass.txt"),
        "az érvényes SSID/jelszó SEM íródott ki (két fázisú mentés)");
  CHECK(ssid[0] == '\0', "a futó konfiguráció (globálisok) is érintetlen");
}

static void scP5() {
  coldBoot(false, "", "", "", "");
  setup();
  g_fsWritable = false;
  const int code = postConfig("MyNetwork", "jelszo", "", "");
  CHECK(code == 500, "HTTP 500 írási hiba esetén");
  CHECK(!restartRequested(), "nem indul újra");
  g_fsWritable = true;
}

static void scP6() {
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
  coldBoot(false, "", "", "", "");
  setup();
  const uint32_t before = apDeadline;
  g_millis += 60000;
  AsyncWebServerRequest req;
  g_handlers["/#1"](&req);
  CHECK(req._code == 200, "a beállító oldal kiszolgálva");
  CHECK(apDeadline > before, "a kérés kitolta az 5 perces határidőt");
}

static void scCPU1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  loop();
  loop();
  CHECK(currentState == (State)1, "várakozás két ellenőrzés között");
  const uint32_t before = g_millis;
  loop();
  CHECK(g_millis > before, "a loop() ténylegesen várakozik (delay), nem pörög");
  CHECK(g_millis - before <= 20, "de csak ~10 ms-ot, a válaszkészség megmarad");
}

static void scCPU2() {
  coldBoot(false, "MyNetwork", "pw", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "monitor mód, first start várakozás");
  const uint32_t before = g_millis;
  loop();
  CHECK(g_millis > before, "a várakozás delay-jel megy");
  CHECK(g_millis - before <= 20, "10 ms-os szemcsézettség");
}

static void scX1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  bool restarted = false;
  int guard = 0;
  try {
    while (!reset_device() && ++guard < 200000) {
      if (guard == 50) g_pinRead[3] = LOW;
      resetbutton(); wifiresetbutton(); delay(10);
    }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a gomb újraindított a pulzus közben");
  CHECK(g_pinState[PIN_RELAY] == HIGH, "a relé ekkor még HIGH volt (router áram nélkül)");

  g_pinRead[3] = HIGH;
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_pinState[PIN_RELAY] == LOW, "az újraindulás után a relé LOW - a router kap áramot");
}

static void scX2() {
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
  coldBoot(false, "", "", "", "");
  setup();
  std::string s32(32, 'A'), s63(63, 'B');
  const int code = postConfig(s32.c_str(), s63.c_str(), "", "");
  CHECK(code == 200, "a pontos maximum még elfogadott");
  CHECK(g_fs["/ssid.txt"].size() == 32, "32 karakteres SSID hiánytalanul mentve");
  CHECK(storedPass().size() == 63, "63 karakteres jelszó hiánytalanul mentve");

  coldBoot(false, "", "", "", "");
  setup();
  std::string s33(33, 'A');
  CHECK(postConfig(s33.c_str(), "pw", "", "") == 500, "33 karakter már elutasítva");
}

static void scX4() {
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "");
  setup();
  CHECK(deviceMode == (DeviceMode)0, "csatlakozott");
  CHECK(wifiSim.configCount == 0, "nem alkalmazott hiányos statikus configot");
  CHECK(!wifiSim.staticApplied, "DHCP-t használ");
}

static void scX5() {
  coldBoot(false, "", "", "", "");
  g_fs["/ssid.txt"] = "   \r\n";
  g_fs["/pass.txt"] = "";
  setup();
  CHECK(ssid[0] == '\0', "a whitespace levágás után üres az SSID");
  CHECK(deviceMode == (DeviceMode)1, "AP mód, nem végzetes hiba");
}

static void scX6() {
  coldBoot(false, "", "", "", "");
  setup();
  g_millis += 4u * 60 * 1000;
  const uint32_t before = apDeadline;
  postConfig("Halozat", "pw", "rossz-ip", "");
  CHECK(apDeadline > before, "a sikertelen mentés is kitolta a határidőt");
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 4u*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "van ideje kijavítani az adatokat");
}

static bool routerAppearsAt(uint32_t appearMs, uint32_t* decidedAtMs) {
  coldBoot(false, "MyNetwork", "titok123", "", "");

  wifiSim.willConnect = true;
  wifiSim.availableFrom = appearMs;
  g_serialLog.clear();
  setup();
  int guard = 0;
  bool slept = false;
  try {
    while (++guard < 2000000) {
      if (deviceMode != (DeviceMode)0) break;
      if (serialHas("Internet ellenorzes.")) break;
      loop();
    }
  } catch (DeepSleepSignal&) { slept = true; }
  *decidedAtMs = g_millis;
  return !slept && deviceMode == (DeviceMode)0 && WiFi.status() == WL_CONNECTED;
}

static void scPO1() {
  uint32_t at = 0;
  const bool ok = routerAppearsAt(8u * 60 * 1000, &at);
  printf("     [info] dontes %u perckor\n", at / 60000);
  CHECK(ok, "8 perces router indulást kivár és csatlakozik");
  CHECK(deviceMode == (DeviceMode)0, "monitor módban működik tovább");
}

static void scPO2() {
  uint32_t at = 0;
  const bool ok = routerAppearsAt(11u * 60 * 1000, &at);
  printf("     [info] dontes %u perckor\n", at / 60000);
  CHECK(ok, "11 percnél is elkapja az újrapróbálkozási ablakban");
}

static void scPO3() {
  uint32_t at = 0;
  bool ok = false;
  try { ok = routerAppearsAt(20u * 60 * 1000, &at); } catch (DeepSleepSignal&) {}
  printf("     [info] dontes %u perckor, mode=%d\n", at / 60000, (int)deviceMode);

  CHECK(ok, "a router reset utani próbálkozás elkapja, még az első körben");
  CHECK(deviceMode == (DeviceMode)0, "monitor módban működik tovább");
}

static void scR1() {
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

  printf("     [info] csatlakozasi probalkozasok az AP modig: %d\n", wifiSim.beginCount);
  CHECK(wifiSim.beginCount >= 2, "egynel tobb probalkozas kellett a felismereshez");
}

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

static void scR6() {
  coldBoot(false, "MyNetwork", "titok123", "", "");
  setup();
  const uint32_t t0 = 0;
  bool slept = false;
  try { while (g_millis < 3u*60*60*1000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  const uint32_t awake = g_millis - t0;
  printf("     [info] boot -> deep sleep: %.1f perc\n", awake/60000.0);
  CHECK(slept, "egy ora alvasra ment, nem AP modba");
  CHECK(awake > 25u*60*1000 && awake < 26u*60*1000,
        "az ebren toltott resz 25 es 26 perc kozott van (a doksi 25,5-ot ir)");
}

static int uptimeSecAt(size_t idx) {
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
    loop();
    g_httpCode = -1;
    pingSim.ok = false;
    g_httpFailMs = failMs;
    g_serialLog.clear(); g_log.clear();
    int guard = 0;
    try { while (logIndex(RELAY_HIGH) < 0 && ++guard < 500000) loop(); }
    catch (DeepSleepSignal&) {}
    g_httpFailMs = 15000; g_httpCode = 200;
    const int elsoTeszt = serialIndex("Internet ellenorzes.");
    const int reset     = serialIndex("Router reset indul.");
    if (elsoTeszt < 0 || reset < 0) return -1;
    return uptimeSecAt((size_t)reset) - uptimeSecAt((size_t)elsoTeszt);
  };
  const int elo = merd(15000);
  printf("     [info] elo DNS, hallgato szerver: %d mp\n", elo);

  CHECK(elo >= 148 && elo <= 152, "elo DNS mellett 150 mp (a doksi szama)");
  const int halott = merd(33000);
  printf("     [info] halott DNS: %d mp\n", halott);
  CHECK(halott >= 166 && halott <= 170, "halott DNS mellett 168 mp");
}

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
  coldBoot(false, "MyNetwork", "titok123", "", "");
  rtcRetryRounds = 32;
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
  const uint32_t roundTenths = 100
                             + 20
                             + 15
                             + 100
                             + 20
                             + 600;
  CHECK(roundTenths == 855, "egy kör 85,5 perc");
  CHECK(33u * roundTenths == 28215u, "33 kör = 2821,5 perc");
  CHECK(28215u < 48u * 60u * 10u, "és ez kevesebb, mint 2 nap (28800 tizedperc)");
  CHECK(34u * roundTenths > 48u * 60u * 10u, "34 kör viszont már túllépné");
}

static void scR4() {
  coldBoot(true, "MyNetwork", "titok123", "", "");
  rtcRetryRounds = 17;
  setup();
  CHECK(deviceMode == (DeviceMode)0, "csatlakozott");
  CHECK(rtcRetryRounds == 0, "új 2 napos ablak indul");
}

static void scL1() {
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

  CHECK(req._body.find(">TEST FAIL<") != std::string::npos, "a friss események láthatók");

  CHECK(req._body.find("<td>fajl</td>") != std::string::npos,
        "a lap a mentett fajl sorait is mutatja");
  CHECK(rtcEvNext > EVLOG_SIZE_C, "az RTC gyuru tenyleg tulcsordult");
  CHECK(req._body.find(">BOOT<") != std::string::npos,
        "a régi BOOT már kicsúszott a körpufferből");
}

static void scL3() {
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
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[3] = LOW;
  bool slept = false; uint64_t us = 0;
  try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "elaludt");
  CHECK(us == 60ULL * 1000000ULL, "60 másodperces ébresztés");
  CHECK(g_gpioWakeMask == 0, "gomb-ébresztés NINCS armolva (boot loop ellen)");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a relé LOW - a router kap áramot");
}

static void scSB2() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[2] = LOW;
  bool slept = false; uint64_t us = 0;
  try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "a wifireset gomb beragadását is elkapja");
  CHECK(us == 60ULL * 1000000ULL, "60 másodperces ébresztés");
  CHECK(g_gpioWakeMask == 0, "gomb-ébresztés NINCS armolva");
}

static void scSB3() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_pinRead[3] = LOW;
  g_log.clear();
  try { setup(); } catch (DeepSleepSignal&) {}

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
  coldBoot(true, "TestNet", "pw", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  g_pinRead[2] = LOW;
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
  coldBoot(false, "MyNetwork", "pw", "", "");
  g_wdtFeedBeforeEnable = 0;
  setup();
  printf("     [info] etetes a feliratkozas elott: %u\n",
         (unsigned)g_wdtFeedBeforeEnable);
  CHECK(g_wdtEnabled, "a watchdog bekapcsolt a setup() végén");
  CHECK(g_wdtFeedBeforeEnable == 0,
        "a feliratkozás ELŐTT egyszer sem etettünk");
}

static void scSN1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  g_gpioWakeResult = -1;
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
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Rossz"; pingSim.ok = false;
  setup();
  loop();
  const uint32_t lpm = serialLinesPerMinute(30u * 60 * 1000);
  printf("     [info] %u sor/perc\n", lpm);
  CHECK(lpm <= 30, "internet kiesésnél is max ~30 sor/perc");
}

static size_t nemHeapSorok(size_t from) {
  size_t n = 0;
  for (size_t i = from; i < g_serialLog.size(); i++) {
    const std::string& l = g_serialLog[i];
    if (l.find("Heap: szabad") != std::string::npos) continue;
    if (l.find("Uptime:") != std::string::npos) continue;
    n++;
  }
  return n;
}

static void scSER3() {
  coldBoot(false, "", "", "", "");
  setup();
  const size_t before = g_serialLog.size();
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 4u*60*1000) loop(); } catch (DeepSleepSignal&) {}
  CHECK(nemHeapSorok(before) == 0,
        "AP módban a loop() a heap-soron kívül egy sort sem ír");
  { const uint32_t lpm = (uint32_t)((g_serialLog.size() - before) * 60000
                                    / (g_millis - t0 ? g_millis - t0 : 1));
    printf("     [info] AP mod: %u sor/perc\n", lpm);
    CHECK(lpm <= 1, "és a teljes kimenet is legfeljebb 1 sor/perc"); }

  coldBoot(true, "TestNet", "pw", "", "");
  g_fsMountOk = false;
  setup();
  const size_t before2 = g_serialLog.size();
  const uint32_t t1 = g_millis;
  try { while (g_millis - t1 < 4u*60*1000) loop(); } catch (DeepSleepSignal&) {}
  CHECK(nemHeapSorok(before2) == 0,
        "hibajelző módban sem ír a villogó ciklus a heap-soron kívül");
}

static uint32_t ser8Fazis = 0;
static void ser8Hook() {
  const bool alacsony = ((g_millis / 20000) % 2) == 0;
  g_freeHeap = alacsony ? 20000 : 40000;
  g_maxAllocHeap = g_freeHeap;
  ser8Fazis++;
}

static void scSER8() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  int guard = 0;
  while (uiFlags.firstStart && ++guard < 2000000) loop();
  ser8Fazis = 0;
  g_onDelay = ser8Hook;
  const uint32_t lpm = serialLinesPerMinute(30u * 60 * 1000);
  g_onDelay = nullptr;
  printf("     [info] a kuszob korul ingadozo heap: %u sor/perc\n", lpm);
  CHECK(lpm <= 30, "a kuszob korul ingadozo heap sem lepi tul a 30 sor/percet");
  CHECK(serialHas("FIGYELEM: alacsony"), "de a figyelmeztetes tenyleg megszolal");
}

static void scSER9() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Rossz"; pingSim.ok = false;
  g_fsCapacity = 24;
  setup();
  const uint32_t lpm = serialLinesPerMinute(90u * 60 * 1000);
  printf("     [info] tartos kieses + mindig buko naplomentes: %u sor/perc\n", lpm);
  CHECK(lpm <= 30, "a buko naplomentes sem arasztja el a soros portot");
  CHECK(serialHas("rovid iras") || serialHas("NEM sikerult"),
        "kozben a hibat tenyleg jelezte");
}

static void scSER10() {
  coldBoot(false, "TestNet", "pw", "", "");
  setup();
  const size_t elotte = g_serialLog.size();
  const uint32_t t0 = g_millis;
  int guard = 0;
  int alvasok = 0;

  try {
    while (alvasok < 5 && ++guard < 5000000) {
      try { loop(); }
      catch (DeepSleepSignal&) {
        alvasok++;
        const uint32_t rounds = rtcRetryRounds;
        const uint32_t most = g_millis;
        coldBoot(false, "TestNet", "pw", "", "", 500, true);
        g_millis = most + 3600u * 1000;
        rtcRetryRounds = rounds;
        setup();
      }
    }
  } catch (RestartSignal&) {}
  const uint32_t percek = (g_millis - t0) / 60000;
  const uint32_t lpm = percek ? (uint32_t)((g_serialLog.size() - elotte) / percek) : 0;
  printf("     [info] %d kor a ketnapos letrabol (%u perc, %u sor osszesen): "
         "%u sor/perc\n", alvasok, percek,
         (unsigned)(g_serialLog.size() - elotte), lpm);
  CHECK(alvasok == 5, "ot teljes kort lejatszottunk");
  CHECK(lpm <= 30, "a ketnapos letra sem lepi tul a 30 sor/percet");
  CHECK(rtcRetryRounds >= 5, "es a korszamlalo tenyleg no");
}

static void scOV1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Rossz"; pingSim.ok = false;
  setup();
  loop();
  int guard = 0;
  uint32_t resets = 0;
  try {
    while (++guard < 2000000) {
      const size_t before = g_log.size();
      loop(); g_millis += 10;
      for (size_t i = before; i < g_log.size(); i++)
        if (g_log[i] == RELAY_HIGH) resets++;
      if (resets >= 3) break;
    }
  } catch (DeepSleepSignal&) {}
  CHECK(resets >= 3, "több router reset ciklus lefutott");
  CHECK(guard < 2000000, "nem ragadt be végtelen ciklusba");
  CHECK(testState.resetEvents == 3, "a számláló pontosan követi a reseteket");
}

static void scWDT6() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtInited = false; g_wdtEnabled = false;
  setup();
  CHECK(logIndex("wdt_init") >= 0, "a sketch inicializalta a TWDT-t");
  CHECK(g_wdtEnabled, "a loop task VEGUL fel van iratkozva");
  CHECK(g_wdtTimeoutMs == 90000 && g_wdtPanic, "90 mp-es timeout, panic bekapcsolva");
  CHECK(serialHas("Watchdog enabled"), "csak most jelenti sikeresnek");

  g_wdtFeedNotSubscribed = 0;
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 60u * 1000 && ++guard < 500000) loop(); }
  catch (...) {}
  CHECK(g_wdtFeedNotSubscribed == 0, "nincs feliratkozas nelkuli etetes (nincs log_e() aradat)");
}

static void scWDT7() {
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

  CHECK(g_serialLog.size() - before < 80,
        "1 perc alatt sem arasztja el a konzolt");
}

static void scWDT8() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtReconfigureFails = true;
  setup();
  CHECK(!serialHas("Watchdog enabled"), "nem jelent sikeres 90 mp-es timeoutot");
  CHECK(serialHas("FIGYELEM"), "figyelmeztet, hogy nincs vedelem");
  CHECK(!g_wdtEnabled, "leiratkozott: az 5 mp-es default nem valhat csapdava");
  CHECK(logIndex("disableLoopWDT") > logIndex("enableLoopWDT"),
        "a leiratkozas a sikertelen konfiguralas KOVETKEZMENYE");

  g_wdtFeedNotSubscribed = 0;
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 30u * 1000 && ++guard < 300000) loop(); }
  catch (...) {}
  CHECK(g_wdtFeedNotSubscribed == 0,
        "leiratkozas utan egyetlen etetes sincs (nincs hibasor-aradat)");
}

static void scL4() {
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

static void scP8() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  int code = postConfig("Halozat", "jelszo123", "192.168.1.200", "", &body);
  CHECK(code == 500, "IP gateway nelkul -> 500, nem hamis siker");
  CHECK(body.find("gateway") != std::string::npos, "az indoklas megnevezi a gateway-t");
  CHECK(!restartRequested(), "nem indul ujra a hianyos konfiggal");

  code = postConfig("Halozat", "jelszo123", "", "192.168.1.1", &body);
  CHECK(code == 500, "gateway IP nelkul -> szinten 500");

  code = postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1", &body);
  CHECK(code == 200, "mindketto megadva -> 200");
  CHECK(restartRequested(), "es most mar ujraindul");
}

static void scP9() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  postConfig("Halozat", "jelszo123", "192.168.1.200", "", &body);
  CHECK(body.find("probald meg ismet") != std::string::npos,
        "a hibauzenet vege sem csonkolodik le");
}

static void scCH1() {
  g_httpCode = 200; g_httpBeginOk = true;
  g_httpChunked = true; g_httpChunkSize = 0;
  g_httpBody = "Microsoft NCSI";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "chunked valasz is egyezik");
  CHECK(!testInternetHTTP("http://x/", "Valami mas"), "de a tartalmat tovabbra is nezi");
}

static void scCH2() {
  g_httpCode = 200; g_httpBeginOk = true;
  g_httpChunked = true; g_httpChunkSize = 3;
  g_httpBody = "Microsoft NCSI";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "tobb darabot is osszefuz");
  g_httpChunkSize = 1;
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "bajtonkent darabolva is");
}

static void scCH3() {
  g_httpCode = 200; g_httpBeginOk = true; g_httpChunked = true;
  g_httpRawOverride = "E;padding=xyz\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "kiterjesztest es nagybetus hexat is kezel");
  g_httpRawOverride = "e\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(testInternetHTTP("http://x/", "Microsoft NCSI"), "kisbetus hexa is jo");
  g_httpRawOverride.clear();
}

static void scCH4() {
  g_httpCode = 200; g_httpBeginOk = true; g_httpChunked = true;
  const uint32_t t0 = g_millis;
  g_httpRawOverride = "zz\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "hexa helyett szemet -> nem egyezik");

  g_httpRawOverride = "5\r\nMic";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "csonka darab -> nem egyezik");

  g_httpRawOverride = "FFFFFFFFFFFF\r\nMicrosoft NCSI\r\n0\r\n\r\n";
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "tulcsordulo darabmeret -> nem egyezik");
  CHECK(g_millis - t0 < 5000, "es egyik esetben sem var ki timeoutot");
  g_httpRawOverride.clear();
}

static void scCH5() {
  g_httpCode = 200; g_httpBeginOk = true; g_httpChunked = true;
  g_httpChunkSize = 100;
  g_httpBody = std::string(50000, 'x');
  const uint32_t t0 = g_millis;
  CHECK(!testInternetHTTP("http://x/", "Microsoft NCSI"), "nem egyezik, es nem is szallt el");
  CHECK(g_millis - t0 < 5000, "nem olvassa vegig az 50 kB-ot");
  g_httpChunkSize = 0; g_httpBody = "Microsoft NCSI";
}

static void scH5() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpCode = 200; g_httpSize = -1; g_httpBody = "Micro";
  g_httpStall = true;
  const uint32_t t0 = g_millis;
  const bool ok = testInternetHTTP("http://pelda/x", "Microsoft NCSI");
  const uint32_t elapsed = g_millis - t0;
  g_httpStall = false;
  CHECK(!ok, "a csonka valasz nem egyezik");
  CHECK(elapsed < 5000, "a hatarido ~1,5 mp korul tart, nem 10 mp-ig var");
  CHECK(elapsed < 90000, "biztosan a watchdog timeout alatt marad");
}

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

  g_httpCode = 204; g_httpBody = "Microsoft Connect Test";
  CHECK(!testInternetHTTP("http://x/", "Microsoft Connect Test"),
        "a szöveges teszt nem fogad el 204-et");
  g_httpCode = 200; g_httpSize = -2;
}

static void scH7() {
  g_httpBeginOk = true; g_httpCode = 204; g_httpBody = ""; g_httpSize = -1;
  const uint32_t t0 = g_millis;
  CHECK(testInternetHTTP("http://x/generate_204", ""), "204 -> siker");
  CHECK(g_millis - t0 < 1500,
        "a törzset el sem kezdi olvasni, nincs 1,5 mp várakozás");
  g_httpCode = 200; g_httpSize = -2;
}

static void scH8() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  g_httpUrls.clear(); pingSim.targets.clear();

  int guard = 0;
  try { while (++guard < 2000000 && g_httpUrls.size() < 5) loop(); }
  catch (DeepSleepSignal&) {}

  CHECK(g_httpUrls.size() == 5, "öt tartalék HTTP teszt futott");
  if (g_httpUrls.size() == 5) {
    CHECK(g_httpUrls[0] == "http://www.msftconnecttest.com/connecttest.txt", "0: Microsoft");
    CHECK(g_httpUrls[1] == "http://cp.cloudflare.com/generate_204", "1: Cloudflare");
    CHECK(g_httpUrls[2] == "http://detectportal.firefox.com/success.txt", "2: Mozilla");
    CHECK(g_httpUrls[3] == "http://nmcheck.gnome.org/check_network_status.txt",
          "3: GNOME / NetworkManager");
    CHECK(g_httpUrls[4] == "http://connectivitycheck.gstatic.com/generate_204", "4: Google");
  }
  bool csakProbeCel = true;
  for (auto& t : pingSim.targets) {
    if (t != "1.1.1.1" && t != "8.8.8.8" && t != "192.168.1.1") csakProbeCel = false;
  }
  CHECK(csakProbeCel, "ping csak az internet és a router címére ment");
}

static void scIP1() {
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
  CHECK(!restartRequested(), "nem indul ujra ervenytelen cimmel");

  code = postConfig("Halozat", "jelszo123", "192.168.1.200", "fe80::1", &body);
  CHECK(code == 500, "IPv4 IP + IPv6 gateway -> 500");

  code = postConfig("Halozat", "jelszo123", "0.0.0.0", "192.168.1.1", &body);
  CHECK(code == 500, "0.0.0.0 sem fogadhato el (a config() DHCP-nek venne)");

  code = postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1", &body);
  CHECK(code == 200, "ervenyes IPv4 paros -> 200");
}

static void scIP3() {
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "fe80::1");
  setup();
  CHECK(wifiSim.configCount == 0, "nem hivott config()-ot a hibas parossal");
  CHECK(!wifiSim.staticApplied, "DHCP-re esett vissza");
  CHECK(serialHas("Invalid gateway format"), "es meg is mondja, miert");
  CHECK(wifiSim.beginCount == 1, "de csatlakozni azert megprobalt");
}

static void scP10() {
  coldBoot(false, "", "", "", "");
  setup();
  (void)beginConfigWrite();
  g_pinRead[3] = LOW;
  bool restarted = false;
  try {
    for (int i = 0; i < 50; i++) { resetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "mentes kozben NEM indul ujra");

  endConfigWrite();
  try {
    for (int i = 0; i < 20; i++) { resetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a mentes utan viszont lefut (uj 50 ms debounce utan)");
}

static void scP11() {
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/ssid.txt"] = "RegiHalozat";
  (void)beginConfigWrite();
  g_pinRead[2] = LOW;
  bool restarted = false;
  try {
    for (int i = 0; i < 50; i++) { wifiresetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "mentes kozben NEM indul ujra");
  CHECK(g_fs["/ssid.txt"] == "RegiHalozat", "es nem is torolte a fajlokat");

  endConfigWrite();
  try {
    for (int i = 0; i < 20; i++) { wifiresetbutton(); delay(10); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a mentes utan viszont torol es ujraindit");
}

static void scP12() {
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig("  MyNetwork  ", "  titok123  ", "", "");
  CHECK(code == 200, "elfogadja");
  CHECK(g_fs["/ssid.txt"] == "MyNetwork", "az elmentett SSID mar vagott");
  CHECK(storedPass() == "titok123", "a jelszo is");
  CHECK(serialHas("SSID set to: MyNetwork"), "es a visszajelzes is a vagott ertek");
}

static void scP13() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("     ", "jelszo123", "", "", &body);
  CHECK(code == 500, "csupa szokoz SSID -> 500");
  CHECK(!restartRequested(), "nem indul ujra hasznalhatatlan SSID-vel");
}

static void scLED1() {
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

static void scP14() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "", "") == 200, "mentes OK");
  CHECK(restartRequested(), "ujrainditas beutemezve");

  bool early = false;
  try {
    for (int i = 0; i < 150; i++) { loop(); }
  } catch (RestartSignal&) { early = true; }
  CHECK(!early, "a 2 mp-es turelmi ido alatt NEM indul ujra");

  bool restarted = false;
  try {
    for (int i = 0; i < 200; i++) { loop(); }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a turelmi ido utan viszont ujraindul");
}

static void scL6() {
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  setup();
  logEvent((EventCode)2, 7);
  logEvent((EventCode)3, 3);
  logEvent((EventCode)4, 1);
  logEvent((EventCode)5, 2);
  logEvent((EventCode)7, 0);
  logEvent((EventCode)8, 4);
  logEvent((EventCode)9, 3);
  logEvent((EventCode)10, 2);
  logEvent((EventCode)11, 1);
  logEvent((EventCode)12, 1);
  logEvent((EventCode)13, 24);
  logEvent((EventCode)14, 11);
  logEvent((EventCode)15, 1011);
  logEvent((EventCode)16, 2);

  AsyncWebServerRequest req;
  g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  const char* want[] = { ">BOOT<", ">WIFI OK<", ">WIFI LOST<", ">TEST FAIL<",
                         ">ROUTER RESET<", ">AP MODE<", ">CONFIG SAVED<",
                         ">SLEEP<", ">FATAL<", ">WDT RESET<", ">STUCK BUTTON<",
                         ">GW UNREACH<", ">LOW HEAP<", ">HEAP RESTART<",
                         ">HTTP ERROR<", ">NET DIAG<" };
  bool all = true;
  for (const char* w : want) if (b.find(w) == std::string::npos) { all = false; printf("     [info] hianyzik: %s\n", w); }
  CHECK(all, "MIND a 16 esemenykod olvashato cimket kap");
  CHECK(b.find(">?<") == std::string::npos, "nincs ismeretlen kod a naplóban");
}

static void scL7() {
  coldBoot(false, "", "", "", "");
  setup();
  rtcEvMagic = 0; rtcEvNext = 0;
  g_fs.erase("/evlog.bin");
  AsyncWebServerRequest req;
  g_handlers["/log#1"](&req);
  CHECK(req._body.find("Nincs rogzitett esemeny") != std::string::npos,
        "ures naplonal ezt irja ki");
  CHECK(req._body.find("<table") == std::string::npos, "es nem rajzol ures tablat");
}

static void scF2() {
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

  int fsUtvonalak = 0;
  for (auto& kv : g_handlers) {
    if (kv.first.rfind("/style.css", 0) == 0) fsUtvonalak++;
    if (kv.first.rfind("/favicon", 0) == 0)   fsUtvonalak++;
    if (kv.first.rfind("/wifimanager", 0) == 0) fsUtvonalak++;
  }
  CHECK(fsUtvonalak == 0, "nincs egyetlen fajlkiszolgalo utvonal sem");
}

static void scF3() {
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/pass.txt"] = "szupertitkos";
  AsyncWebServerRequest req;
  g_handlers["404"](&req);
  CHECK(req._code == 404, "ismeretlen utvonal -> 404");
  CHECK(req._body.find("szupertitkos") == std::string::npos, "nem szivarog jelszo");
}

static void scF4() {
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

  const uint32_t third = apDeadline;
  g_millis += 60u * 1000;
  AsyncWebServerRequest c; g_handlers["404"](&c);
  CHECK(c._code == 404, "ismeretlen utvonal tovabbra is 404");
  CHECK(apDeadline > third, "es a 404 is kitolja a hataridot");

  CHECK(apDeadline == g_millis + 5u * 60 * 1000,
        "a hatarido pontosan 5 perccel a keres utanra kerul");
}

static void scP15() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  std::string longPass(80, 'x');
  const int code = postConfig("Halozat", longPass.c_str(), "", "", &body);
  CHECK(code == 500, "tul hosszu jelszo -> 500");
  CHECK(body.find("jelszo") != std::string::npos, "az indoklas a jelszot nevezi meg");
  CHECK(!restartRequested(), "nem indul ujra");
}

static void scE6() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpBody = "Microsoft Connect Test";
  loop();
  rtcRetryRounds = 3;
  wifiSim.begun = false;
  wifiSim.willConnect = true;
  g_log.clear();
  int guard = 0;
  try { while (++guard < 100000 && logIndex(RELAY_HIGH) < 0
               && !serialHas("Internet ellenorzes.")) loop(); }
  catch (...) {}
  CHECK(logIndex(RELAY_HIGH) < 0, "NEM indit routert, ha visszajott a WiFi");
  CHECK(serialHas("WIFI RECONNECTED!"), "ujracsatlakozott");
  CHECK(rtcRetryRounds == 0, "a sikeres teszt nullazza a 2 napos ablakot");
}

static void scX7() {
  coldBoot(true, "TestNet", "pw", "192.168.1.5", "192.168.1.1");
  setup();
  g_log.clear(); g_serialLog.clear();
  g_pinRead[2] = LOW;
  try { for (int i = 0; i < 20; i++) { wifiresetbutton(); delay(10); } }
  catch (RestartSignal&) {}

  int iSsid = serialIndex("Clearing file: /ssid.txt");
  int iPass = serialIndex("Clearing file: /pass.txt");
  int iIp   = serialIndex("Clearing file: /ip.txt");
  int iGw   = serialIndex("Clearing file: /gateway.txt");
  CHECK(iSsid >= 0 && iPass >= 0 && iIp >= 0 && iGw >= 0, "mind a negy fajlt erinti");
  CHECK(iSsid < iPass && iSsid < iIp && iSsid < iGw, "az SSID torlese az ELSO");
}

static void scX8() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  rtcEvMagic = 0; rtcEvNext = 0;
  g_fsWritable = false; g_fsRemoveOk = false;
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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fsWritable = false; g_fsRemoveOk = false;
  g_pinRead[2] = LOW;
  g_log.clear();
  try { for (int i = 0; i < 200000; i++) { wifiresetbutton(); delay(10); } }
  catch (...) {}
  int together = 0, opposite = 0;
  for (size_t i = 0; i + 1 < g_log.size(); i++) {
    if (g_log[i] == "pin6=HIGH" && g_log[i+1] == "pin5=HIGH") together++;
    if (g_log[i] == "pin6=LOW"  && g_log[i+1] == "pin5=LOW")  together++;
    if (g_log[i] == "pin6=HIGH" && g_log[i+1] == "pin5=LOW")  opposite++;
    if (g_log[i] == "pin6=LOW"  && g_log[i+1] == "pin5=HIGH") opposite++;
  }
  CHECK(together > 100, "a ket LED EGYUTT villog (vegzetes hiba jelzese)");
  CHECK(opposite == 0, "sosem ellentetes fazisban (az a beragadt gomb jele)");
}

static void scX13() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fsWritable = false; g_fsRemoveOk = false;
  g_pinRead[2] = LOW;

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

static void scX9() {
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/ssid.txt"] = "EredetiHalozat";
  AsyncWebServerRequest req;
  req.addParam("ssid", "TamadoHalozat", false);
  req.addParam("pass", "tamado", false);
  g_handlers["/#2"](&req);
  CHECK(g_fs["/ssid.txt"] == "EredetiHalozat", "a query parameter NEM irja at az SSID-t");
  CHECK(req._code == 500, "SSID nelkuli mentes -> 500");
  CHECK(!restartRequested(), "es nem indit ujra");
}

static void scX10() {
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

static void scX11() {
  coldBoot(true, "TestNet", "pw", "192.168.1.5", "192.168.1.1");
  wifiSim.configFails = true;
  setup();
  CHECK(serialHas("STA Failed to configure"), "jelzi a hibat");
  CHECK(wifiSim.beginCount >= 1, "de azert megprobal csatlakozni");
  CHECK(deviceMode == (DeviceMode)0, "es monitor modban marad");
}

static void scFS7() {
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
  coldBoot(false, "", "", "", "");
  setup();
  g_fsSilentWriteFail = true;
  std::string body;
  const int code = postConfig("Halozat", "jelszo123", "", "", &body);
  g_fsSilentWriteFail = false;
  CHECK(code == 500, "nema irashiba -> 500, nem hamis siker");
  CHECK(!restartRequested(), "es nem indul ujra hasznalhatatlan konfiggal");
}

static void scFS9() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_fsShortRead = true;
  try { setup(); } catch (DeepSleepSignal&) {}
  g_fsShortRead = false;
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL - serult konfig");
  CHECK(serialHas("short read"), "jelzi a csonka olvasast");
  CHECK(wifiSim.beginCount == 0, "es meg csak meg sem probal csatlakozni");
}

static void scFS10() {
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

static void scKA1() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(g_handlers.count("/ping#1") == 1, "van /ping vegpont");
  AsyncWebServerRequest r; g_handlers["/ping#1"](&r);
  CHECK(r._code == 200, "/ping -> 200");
  CHECK(r._body.size() <= 4, "a valasz aprocska (percenkent fut)");
}

static void scKA2() {
  coldBoot(false, "", "", "", "");
  setup();
  bool slept = false;
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 20u * 60 * 1000) {
      { AsyncWebServerRequest r; g_handlers["/ping#1"](&r); }
      const uint32_t p0 = g_millis;
      while (g_millis - p0 < 60u * 1000) loop();
    }
  } catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "20 percig nyitva tartott lap mellett NEM alszik el");
  CHECK(deviceMode == (DeviceMode)1, "vegig AP konfig modban marad");
}

static void scKA3() {
  coldBoot(false, "", "", "", "");
  setup();
  { AsyncWebServerRequest r; g_handlers["/ping#1"](&r); }
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
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(wifiSim.softApCount == 1, "az AP elindult");
  CHECK(wifiSim.apPass.size() >= 8 && wifiSim.apPass.size() <= 63,
        "az AP jelszo a WPA2 tartomanyban van (8-63 karakter)");
}

static void scSE1() {
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

  std::string maxPw(63, 'Z');
  char enc[3 + 2*63 + 1];
  CHECK(encodeSecret(maxPw.c_str(), enc, sizeof(enc)), "63 karakter is belefer");
  char dec[200]; strlcpy(dec, enc, sizeof(dec)); decodeSecretInPlace(dec);
  CHECK(std::string(dec) == maxPw, "es hibatlanul jon vissza");

  char kicsi[8];
  CHECK(!encodeSecret("hosszabb jelszo", kicsi, sizeof(kicsi)),
        "szuk puffernel false, nem tulcsordulas");
}

static void scSE2() {
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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  char enc[200];
  encodeSecret("aaaaaaaaaaaaaaaa", enc, sizeof(enc));
  std::set<std::string> parok;
  for (size_t i = 3; i + 2 <= strlen(enc); i += 2) parok.insert(std::string(enc + i, 2));
  CHECK(parok.size() >= 12, "16 azonos karakterbol legalabb 12 kulonbozo bajt lesz");
}

static void scSE4() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_efuseMac = 0x0000A1B2C3D4E5F6ULL;
  setup();
  char enc[200];
  encodeSecret("SzuperTitkosJelszo42", enc, sizeof(enc));

  char dec[200]; strlcpy(dec, enc, sizeof(dec));
  g_efuseMac = 0x0000FFEEDDCCBBAAULL;
  decodeSecretInPlace(dec);
  CHECK(std::string(dec) != "SzuperTitkosJelszo42",
        "masik lapkan nem adja vissza a jelszot");

  strlcpy(dec, enc, sizeof(dec));
  g_efuseMac = 0x0000A1B2C3D4E5F6ULL;
  decodeSecretInPlace(dec);
  CHECK(std::string(dec) == "SzuperTitkosJelszo42", "a sajat lapkajan viszont igen");
}

static void scSE5() {
  coldBoot(true, "TestNet", "RegiNyiltJelszo", "", "");
  setup();
  CHECK(std::string(pass) == "RegiNyiltJelszo", "a regi nyilt mentes betoltodik");
  CHECK(wifiSim.beginCount == 1, "es csatlakozik vele");
  CHECK(deviceMode == (DeviceMode)0, "monitor mod");
}

static void scSE6() {
  coldBoot(true, "TestNet", "", "", "");
  g_fs["/pass.txt"] = "v1:zzzz";
  setup();
  CHECK(deviceMode != (DeviceMode)2, "NEM vegzetes hiba");
  CHECK(std::string(pass) == "v1:zzzz", "sima szovegkent kezeli");

  coldBoot(true, "TestNet", "", "", "");
  g_fs["/pass.txt"] = "v1:abc";
  setup();
  CHECK(deviceMode != (DeviceMode)2, "paratlan hosszu hexa sem vegzetes");
}

static void scSE7() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("OtthoniWifi", "SzuperTitkosJelszo42", "", "") == 200, "mentes OK");
  auto mentett = g_fs;
  coldBoot(true, "", "", "", "");
  g_fs = mentett;
  setup();
  CHECK(std::string(ssid) == "OtthoniWifi", "SSID betoltve");
  CHECK(std::string(pass) == "SzuperTitkosJelszo42", "jelszo helyesen visszafejtve");
  CHECK(deviceMode == (DeviceMode)0, "monitor mod - csatlakozott");
}

static void scSE8() {
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
  const char* PW = "MasikTitok99";
  coldBoot(false, "", "", "", "");
  setup();
  postConfig("OtthoniWifi", PW, "", "");
  auto mentett = g_fs;

  coldBoot(true, "", "", "", "");
  g_fs = mentett;
  setup();
  g_httpBody = "Microsoft Connect Test";
  loop();
  const int elozo = wifiSim.beginCount;
  wifiSim.begun = false;
  wifiSim.lastPass.clear();
  int guard = 0;
  try { while (++guard < 100000 && !serialHas("WIFI RECONNECTED!")) loop(); }
  catch (...) {}
  CHECK(wifiSim.beginCount > elozo, "ujra probalt csatlakozni");
  CHECK(wifiSim.lastPass == PW, "az ujracsatlakozas is a nyilt jelszoval megy");
  CHECK(serialHas("WIFI RECONNECTED!"), "es sikerult");
}

static void scSE10() {
  const char* PW = "HarmadikTitok7";
  coldBoot(false, "", "", "", "");
  setup();
  postConfig("OtthoniWifi", PW, "", "");
  auto mentett = g_fs;

  coldBoot(true, "", "", "", "");
  g_fs = mentett;
  setup();
  g_httpBody = "Rossz"; pingSim.ok = false;
  loop();

  int guard = 0;
  try { while (++guard < 400000 && logIndex(RELAY_HIGH) < 0) loop(); }
  catch (...) {}
  CHECK(logIndex(RELAY_HIGH) >= 0, "router reset elindult");
  wifiSim.lastPass.clear(); g_serialLog.clear();
  try { while (++guard < 900000 && !serialHas("WIFI RECONNECTED!")) loop(); }
  catch (...) {}
  CHECK(wifiSim.lastPass == PW, "a router reset utani csatlakozas is a nyilt jelszoval megy");
}

static int gwFutas(bool gwElerheto, bool statikus, uint32_t maxKor) {
  coldBoot(true, "TestNet", "pw",
           statikus ? "192.168.0.200" : "", statikus ? "192.168.0.1" : "");
  setup();
  g_httpBody = "Rossz";
  pingSim.ok = false;
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
  const int resetek = gwFutas(true, true, 400000);
  CHECK(deviceMode != (DeviceMode)1, "NEM megy AP modba");
  CHECK(resetek >= 1, "a gateway el, az internet nem: reset (15 perc utan)");
  CHECK(evCount(EVC_NET_DIAG, 2) >= 1 && evCount(EVC_NET_DIAG, 1) == 0, "NET DIAG 2");
}

static void scGW2() {
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
  const int resetek = gwFutas(false, false, 400000);
  CHECK(deviceMode != (DeviceMode)1, "DHCP-nel nem megy AP modba");
  CHECK(resetek >= 1, "a router pingre nem valaszol: router hibakent reset");
  CHECK(!serialHas("sajat gateway"), "a statikus gateway-ellenorzes nem fut");
}

static void scP16() {
  struct { const char* ip; const char* gw; int varhato; } esetek[] = {
    { "192.168.1.300", "192.168.1.1", 500 },
    { "192.168..1.20", "192.168.1.1", 500 },
    { "192.168.1.200", "192.168.1",   500 },
    { "192.168.1.abc", "192.168.1.1", 500 },
    { "fe80::1",       "192.168.1.1", 500 },
    { "0.0.0.0",       "192.168.1.1", 500 },
    { "192.168.1.200", "",            500 },
    { "192.168.1.200", "192.168.1.1", 200 },
  };

  bool mind = true;
  for (auto& e : esetek) {
    coldBoot(false, "", "", "", "");

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

static uint32_t g_saveEndsAt = 0;
static void saveFinisher() {
  if (g_saveEndsAt && g_millis >= g_saveEndsAt) { endConfigWrite(); g_saveEndsAt = 0; }
}

static void scWR1() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "", "") == 200, "elso mentes OK");
  CHECK(restartRequested(), "ujrainditas beutemezve");

  (void)beginConfigWrite();
  g_saveEndsAt = g_millis + 2500;
  g_onDelay = saveFinisher;

  bool restarted = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 500; i++) loop(); }
  catch (RestartSignal&) { restarted = true; }
  g_onDelay = nullptr;

  CHECK(restarted, "vegul ujraindul");
  CHECK(g_millis - t0 >= 2500, "de csak azutan, hogy az iras befejezodott");

  CHECK(configWriteInProgress(), "es a zar most MAR a mienk - uj mentes nem indulhat");
  CHECK(serialHas("Fajliras folyik"), "jelzi is, hogy var");
  CHECK(serialHas("A fajliras befejezodott"), "es hogy kesz");
}

static void scWR2() {
  coldBoot(false, "", "", "", "");
  setup();
  apDeadline = g_millis;
  (void)beginConfigWrite();
  g_saveEndsAt = g_millis + 800;
  g_onDelay = saveFinisher;

  bool slept = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 500; i++) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  g_onDelay = nullptr;

  CHECK(slept, "vegul elalszik");
  CHECK(g_millis - t0 >= 800, "de csak a fajliras utan");
  CHECK(configWriteInProgress(), "es a zar az elalvasig a mienk marad (lasd SH1)");
}

static void scWR3() {
  coldBoot(false, "", "", "", "");
  setup();
  postConfig("Halozat", "jelszo123", "", "");
  (void)beginConfigWrite();
  bool restarted = false;
  const uint32_t t0 = g_millis;
  try { for (int i = 0; i < 5000; i++) loop(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "beragadt jelzo eseten is ujraindul");
  CHECK(g_millis - t0 >= 5000, "de csak az 5 mp-es hatarido utan");
  CHECK(g_millis - t0 < 8000, "es nem var a vegtelensegig");
  CHECK(serialHas("sem fejezodott be"), "figyelmeztet a beragadt jelzore");
  endConfigWrite();
}

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
  wrapFutas(0xFFFFFFFFu - 240000u);
  CHECK(mw_wrapok == 1, "a millis() tenyleg korbefordult kozben");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000,
        "a rele pulzus a korbefordulas alatt is 90 mp");
}

static void scMW2() {
  wrapFutas(0xFFFFFFFFu - 700000u);
  CHECK(mw_wrapok == 1, "korbefordult a RESET_DELAY alatt");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000, "a pulzus akkor is 90 mp");
  CHECK(g_wakeupUs == 3600000000ULL, "a vegen ugyanugy 1 ora alvas");

  wrapFutas(0xFFFFFFFFu - 180000u);
  CHECK(mw_wrapok == 1, "korbefordult a hibakezeles kozepen");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000, "a pulzus akkor is 90 mp");
}

static void scMW3() {
  wrapFutas(1716698112u);
  CHECK(mw_wrapok == 0, "ebben a szakaszban nincs wrap");
  CHECK(mw_pulzus >= 89000 && mw_pulzus <= 92000, "90 mp-es pulzus 169 nap utan is");
  CHECK(rtcRetryRounds == 1, "a szamlalo normalisan lep");
  CHECK(g_wakeupUs == 3600000000ULL, "1 ora alvas");
}

static void wdLepes() { loop(); }
static uint32_t wdMeres(uint32_t futasMs) {
  g_wdtTrack = true; g_wdtLastFeed = g_millis; g_wdtMaxFeedGap = 0;
  const uint32_t t0 = g_millis;
  try { int g = 0; while (++g < 3000000 && g_millis - t0 < futasMs) wdLepes(); }
  catch (...) {}
  g_wdtTrack = false;
  return g_wdtMaxFeedGap;
}

static void scWD7() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpCode = -1; pingSim.ok = false;
  loop();
  const uint32_t gap = wdMeres(60u * 60 * 1000);
  printf("     [info] leghosszabb etetes nelkuli szakasz: %.1f mp\n", gap/1000.0);
  CHECK(gap >= 14000, "a HTTP timeout tenyleg blokkol (a meres ervenyes)");
  CHECK(gap < 90000, "es a 90 mp-es watchdog timeout alatt marad");
}

static void scWD13() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_httpCode = -1; pingSim.ok = false;
  g_httpFailMs = 33000;
  loop();
  const uint32_t gap = wdMeres(60u * 60 * 1000);
  g_httpFailMs = 15000;
  printf("     [info] halott DNS mellett a leghosszabb szakasz: %.1f mp\n", gap/1000.0);
  CHECK(gap >= 32000, "a 33 mp-es DNS blokkolas tenyleg beleszamit (a meres ervenyes)");
  CHECK(gap < 90000, "es meg igy is a 90 mp-es watchdog timeout alatt marad");
}

static void scWD8() {
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
  coldBoot(true, "TestNet", "pw", "", "");
  g_wdtEnabled = false;

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
  coldBoot(false, "", "", "", "");
  g_wdtEnabled = false;
  g_wdtFeedNotSubscribed = 0;
  setup();
  CHECK(g_wdtFeedNotSubscribed == 0, "a felelesztes elott nem etetunk");
  CHECK(g_wdtEnabled, "a setup() vegere aktiv a watchdog");
  CHECK(g_wdtTimeoutMs == 90000 && g_wdtPanic, "90 mp, panic bekapcsolva");
}

static void scWD12() {
  coldBoot(false, "TestNet", "pw", "", "");
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
  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  g_heldPins.insert(PIN_RELAY); g_deepSleepHoldEnabled = true;
  setup();
  CHECK(g_heldPins.empty(), "a hold ébredés után feloldva");
  CHECK(!g_deepSleepHoldEnabled, "a globális deep sleep hold kikapcsolva");
  { const int drive = logIndex(RELAY_LOW);
    const int release = logIndex("gpio_hold_dis(10)");
    CHECK(drive >= 0 && release >= 0 && drive < release,
          "előbb a meghajtott LOW, aztán a hold feloldása"); }
}

static void scS6() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_gpioHoldFails = true;
  const bool slept = egyResetUtanAlvas() != UINT64_MAX;
  CHECK(slept, "az alvás a hold hibája ellenére megtörténik");
  CHECK(!g_deepSleepHoldEnabled, "hibás hold_en után nincs deep sleep hold");
  CHECK(serialHas("FIGYELEM: a rele lab rogzitese"), "a hiba nem néma");
}

static void scSB5() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  g_pinRead[2] = LOW;
  try { setup(); } catch (DeepSleepSignal&) {}
  const uint32_t afterFirst = rtcEvNext;
  CHECK(afterFirst == 2, "az első kör két bejegyzés: BOOT + STUCK BUTTON");
  for (int wake = 0; wake < 5; wake++) {
    coldBoot(true, "TestNet", "pw", "", "", 500, true);
    g_pinRead[2] = LOW;
    try { setup(); } catch (DeepSleepSignal&) {}
  }
  CHECK(rtcEvNext == afterFirst, "az ismétlődő ébredések nem írnak új bejegyzést");
  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  setup();
  CHECK(rtcEvNext > afterFirst, "a kiszabadulás utáni boot újra naplóz");
}

static void scP17() {
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig("MyNetwork", "jelszo", " 192.168.1.200 ", "192.168.1.1 ");
  CHECK(code == 200, "szóközös IP/gateway elfogadva (vágás után)");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200", "az IP vágva mentődik");
  CHECK(g_fs["/gateway.txt"] == "192.168.1.1", "a gateway vágva mentődik");

  const char* oversize = "192.168.1.200                    X";
  const int code2 = postConfig("MyNetwork", "jelszo", oversize, "192.168.1.1");
  CHECK(code2 == 500, "túlméretes IP érték hiba, nem csonkolt elfogadás");
}

static void scP18() {
  coldBoot(false, "", "", "", "");
  setup();
  const int code = postConfig("MyNetwork", "jelszo", "   ", "  ");
  CHECK(code == 200, "csupa szóköz IP/gateway nem hiba");
  CHECK(g_fs["/ip.txt"].empty() && g_fs["/gateway.txt"].empty(), "DHCP-ként mentve");
}

static void scP19() {
  coldBoot(false, "", "", "", "");
  setup();
  (void)beginConfigWrite();
  const int code = postConfig("MyNetwork", "jelszo", "", "");
  CHECK(code == 503, "zárolt konfignál 503, nem néma felülírás");
  CHECK(!g_fs.count("/ssid.txt"), "fájl nem íródott");
  CHECK(!restartRequested(), "újraindítás sincs beütemezve");
  CHECK(configWriteInProgress(), "a más által tartott zárat nem engedte el");
  endConfigWrite();
  const int code2 = postConfig("MyNetwork", "jelszo", "", "");
  CHECK(code2 == 200, "a zár felszabadulása után a mentés már lefut");
}

static void scP20() {
  coldBoot(false, "", "", "", "");
  setup();

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

static int fsIrasokSzama() {
  int n = 0;
  for (auto& l : g_log) {
    if (l.rfind("fs_write", 0) == 0 || l.rfind("fs_open_w", 0) == 0) n++;
  }
  return n;
}

static void scOP1() {
  coldBoot(true, "MyNetwork", "titok123", "", "");
  wifiSim.availableFrom = 3u * 60 * 1000;
  pingSim.ok = true;
  setup();
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
  coldBoot(true, "MyNetwork", "titok123", "", "");
  wifiSim.availableFrom = 3u * 60 * 1000;
  pingSim.ok = false;
  setup();

  const uint32_t t0 = timing.startMillis;
  int guard = 0;
  try { while (uiFlags.firstStart && ++guard < 400000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(g_millis - t0 >= 10u * 60 * 1000, "a teljes 10 perc lefutott");
  CHECK(!serialHas("halozat es internet visszajott"), "nem volt korai kilepes");
  CHECK(!pingSim.targets.empty(), "a proba azert probalkozott");
}

static void scOP3() {
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
  coldBoot(true, "MyNetwork", "titok123", "", "");
  wifiSim.availableFrom = 0xFFFFFFFFu;
  pingSim.ok = false;
  setup();
  const int beginBefore = wifiSim.beginCount;
  const size_t fsBefore = g_fs.size();

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

static uint32_t merjGombHezag(void (*forgatokonyv)()) {
  g_btnTrack = true; g_btnLastPoll = g_millis; g_btnMaxGap = 0;
  forgatokonyv();
  g_btnTrack = false;
  return g_btnMaxGap;
}

static uint32_t s_wifiLedOffAt = 0;
static void figyelWifiLed() {
  if (s_wifiLedOffAt == 0 && g_pinState[PIN_WIFILED] != HIGH) s_wifiLedOffAt = g_millis;
}

static void scRR3() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; g_httpFailMs = 15000; pingSim.ok = false;
  setup();
  int guard = 0, resetek = 0;
  size_t nezettIg = 0;
  try {
    while (++guard < 2000000 && resetek < 2) {
      loop();
      for (size_t i = nezettIg; i < g_log.size(); i++)
        if (g_log[i] == RELAY_HIGH) resetek++;
      nezettIg = g_log.size();
    }
  } catch (DeepSleepSignal&) {}
  CHECK(resetek == 2, "ket reset megtortent");
  CHECK(testState.resetEvents == 2, "a szamlalo 2-n all");

  g_httpCode = 200; g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  guard = 0;
  try { while (++guard < 400000 && !serialHas("Successful Test")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Successful Test"), "sikeres ellenorzes");
  CHECK(testState.resetEvents == 0 && !testState.outage, "a szamlalo es a kieses NULLAZODOTT");
}

static void gombNyomas(int pin, uint32_t tartasMs) {
  g_pinRead[pin] = LOW;  simIsr(pin);
  g_millis += tartasMs;
  g_pinRead[pin] = HIGH; simIsr(pin);
}

static void scAP2() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "SzuperTitkos42", "192.168.1.200", "192.168.1.1") == 200,
        "mentes");
  AsyncWebServerRequest g; g_handlers["/#1"](&g);
  CHECK(g._code == 200, "a / 200-at ad");
  CHECK(g._body.find("SzuperTitkos42") == std::string::npos,
        "a NYILT jelszo NINCS a lapon");
  CHECK(g._body.find("v1:") == std::string::npos,
        "a KODOLT jelszo sincs a lapon");
  CHECK(g._body.find("type=\"password\"") != std::string::npos,
        "a jelszo mezo type=password (nem latszik gepeles kozben)");

  const size_t pw = g._body.find("name=\"pass\"");
  const size_t ipm = g._body.find("name=\"ip\"");
  CHECK(pw != std::string::npos && ipm != std::string::npos && pw < ipm, "a mezok sorrendje");
  CHECK(g._body.substr(pw, ipm - pw).find("value=") == std::string::npos,
        "a jelszo mezonek NINCS elokitoltott erteke");

  CHECK(g._body.find("value=\"Halozat\"") != std::string::npos, "az SSID elokitoltve");
  CHECK(g._body.find("value=\"192.168.1.200\"") != std::string::npos, "az IP elokitoltve");
  CHECK(g._body.find("value=\"192.168.1.1\"") != std::string::npos, "a gateway elokitoltve");
}

static void scAP3() {
  coldBoot(false, "", "", "", "");
  setup();
  const char* gonosz = "a\"><script>x</script>&b";
  CHECK(postConfig(gonosz, "jelszo123", "", "") == 200, "a gonosz SSID elmentheto");
  AsyncWebServerRequest g; g_handlers["/#1"](&g);

  CHECK(g._body.find("<script>x</script>") == std::string::npos,
        "a beinjektalt <script>x</script> NEM kerul nyersen a lapba");
  CHECK(g._body.find("\"><script") == std::string::npos,
        "es az attributumbol sem lehet kitorni");
  CHECK(g._body.find("&lt;script&gt;") != std::string::npos, "hanem escape-elve");
  CHECK(g._body.find("&quot;") != std::string::npos, "az idezojel is escape-elve");
  CHECK(g._body.find("&amp;b") != std::string::npos, "es az & is");

  CHECK(g._body.find("</form>") != std::string::npos, "az urlap szerkezete ep");
}

static void scAP4() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1") == 200, "elso mentes");
  clearRestartRequest();

  AsyncWebServerRequest g; g_handlers["/#1"](&g);
  const size_t vi = g._body.find("name=\"ip\"");
  const size_t vv = g._body.find("value=\"", vi) + 7;
  const std::string lathatoIp = g._body.substr(vv, g._body.find('"', vv) - vv);
  CHECK(lathatoIp == "192.168.1.200", "a lapon a mentett IP latszik");

  CHECK(postConfig("Halozat", "ujJelszo456", lathatoIp.c_str(), "192.168.1.1") == 200,
        "a felhasznalo csak a jelszot irja at, a cimeket a lap kitoltve adta");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200", "a statikus IP MEGMARADT");
  CHECK(g_fs["/gateway.txt"] == "192.168.1.1", "es a gateway is");
}

static uint32_t s_pislakolUtolso = 0;
static uint32_t s_pislakolUtem = 5000;
static void pislakol() {
  if (g_millis - s_pislakolUtolso >= s_pislakolUtem) {
    s_pislakolUtolso = g_millis;
    wifiSim.willConnect = !wifiSim.willConnect;
  }
}

static void merjPislakolas(uint32_t utem, uint32_t& bejegyzes, int& maxEgymasUtan,
                           uint32_t& sorPerc) {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  setup();
  int guard = 0;
  try { while (++guard < 50000 && uiFlags.firstStart) loop(); }
  catch (DeepSleepSignal&) {}
  rtcEvMagic = 0; rtcEvNext = 0;
  g_serialLog.clear();
  s_pislakolUtem = utem; s_pislakolUtolso = g_millis;
  g_onDelay = pislakol;
  const uint32_t t0 = g_millis;
  guard = 0;
  try { while (++guard < 400000 && g_millis - t0 < 15u * 60 * 1000) loop(); }
  catch (DeepSleepSignal&) {}
  g_onDelay = nullptr;
  bejegyzes = rtcEvNext;
  int egymas = 0; maxEgymasUtan = 0;
  const uint32_t megvan = rtcEvNext < 32 ? rtcEvNext : 32;
  for (uint32_t i = rtcEvNext - megvan; i < rtcEvNext; i++) {
    if (rtcEvents[i % 32].code == 3) { egymas++;
      if (egymas > maxEgymasUtan) maxEgymasUtan = egymas; }
    else egymas = 0;
  }
  sorPerc = (uint32_t)(g_serialLog.size() * 60000ull / (g_millis - t0));
}

static void scLOG4() {
  // Nem osztoi a 60 mp-es utemnek, kulonben az ellenorzes mindig ugyanabba a fazisba esne.
  const uint32_t utemek[] = { 700, 1300, 2300, 4700 };
  for (uint32_t u : utemek) {
    uint32_t bej, spm; int maxEgymas;
    merjPislakolas(u, bej, maxEgymas, spm);
    printf("     [info] %4u ms-os pislakolas: %4u bejegyzes, max %d egymas utani "
           "WIFI LOST, %u sor/perc\n", u, bej, maxEgymas, spm);
    CHECK(bej <= 60, "a naplo nem arad el");
    CHECK(maxEgymas <= 1, "nincs ket egymas utani WIFI LOST");
    CHECK(spm <= 120, "a soros kimenet is kordaban marad");
  }
}

static void scLOG5() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  g_criticalMaxDepth = 0;
  setup();
  const size_t fsElotte = g_fs.size();
  int guard = 0;
  try { while (++guard < 200000 && !serialHas("Router reset indul.")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(rtcEvNext > 3, "tenylegesen naplóztunk esemenyeket");
  CHECK(g_criticalMaxDepth == 1,
        "a kritikus szakasz sosem agyazodik egymasba (max melyseg 1)");

  {
    size_t ujak = 0;
    for (auto& kv : g_fs) if (kv.first == "/evlog.bin") ujak++;
    CHECK(g_fs.size() <= fsElotte + ujak,
          "a naplozas a naplofajlon kivul egyetlen fajlt sem hozott letre");
  }

  g_fs.clear();
  const uint32_t elotte = rtcEvNext;
  logEvent((EventCode)2, 7);
  CHECK(rtcEvNext == elotte + 1,
        "ures fajlrendszerrel is naplozhatunk - a naplo nem fugg tole");
}

static void scLOG1() {
  coldBoot(false, "", "", "", "", 500, true);
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk, a portal fut");
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  CHECK(req._code == 200, "a /log 200-at ad");
  CHECK(b.find("ebredes deep sleepbol") != std::string::npos,
        "az indulas oka SZOVEGESEN is szerepel");
  CHECK(b.find("(8)") != std::string::npos,
        "a nyers szam zarojelben megmarad (hibajelenteshez)");
  CHECK(b.find("Uptime:</b> 0d 0h") != std::string::npos,
        "az uptime nap/ora/perc/mp alakban, nem nyers masodpercben");
  CHECK(b.find("Param jelentese") != std::string::npos,
        "a Param oszlop jelmagyarazata ott van a lapon");
  CHECK(b.find("4 = a gateway sem erheto el") != std::string::npos,
        "es tenyleg megfejtheto belole egy kod");

  rtcEvMagic = 0; rtcEvNext = 0;
  g_fs.erase("/evlog.bin");
  AsyncWebServerRequest ures; g_handlers["/log#1"](&ures);
  CHECK(ures._body.find("Param jelentese") != std::string::npos,
        "a jelmagyarazat ures naplónal is ott van");
  CHECK(ures._body.find(">BOOT<") == std::string::npos,
        "de NEM allitja elo a tablazatcella-mintat");
}

static void scLOG2() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("TitkosHalozat", "SzuperTitkos42", "192.168.1.200", "192.168.1.1") == 200,
        "mentes");
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  CHECK(b.find("SzuperTitkos42") == std::string::npos, "a nyilt jelszo NINCS a naplon");
  CHECK(b.find("v1:") == std::string::npos, "a kodolt jelszo sincs");
  CHECK(b.find("TitkosHalozat") == std::string::npos, "az SSID sincs");
  CHECK(b.find("192.168.1.200") == std::string::npos, "es a mentett IP sem");

  CHECK(b.find("CONFIG SAVED") != std::string::npos, "a mentes TENYE viszont igen");
}

static void scLOG3() {
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0; rtcEvNext = 0;
  setup();
  for (int i = 0; i < 40; i++) logEvent((EventCode)2, (uint16_t)i);
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  size_t sorok = 0, tol = 0;
  while ((tol = b.find("<tr><td>", tol)) != std::string::npos) { sorok++; tol += 8; }

  size_t rtcSor = 0, rp = 0;
  while ((rp = b.find("<td>RTC</td>", rp)) != std::string::npos) { rtcSor++; rp++; }
  CHECK(rtcSor <= 32, "az RTC reszbol legfeljebb 32 sor (a korpuffer merete)");
  CHECK(sorok >= rtcSor, "az osszes sor legalabb ennyi");
  CHECK(b.find("<td>39</td>") != std::string::npos, "a LEGUJABB esemeny benne van");
  CHECK(b.find("<td>7</td>") == std::string::npos, "a kiszorult regi mar nincs");
  CHECK(b.find("</table>") != std::string::npos, "a tabla rendesen lezarul");
}

static void monitorUzembe() {
  g_httpBody = "Microsoft Connect Test";
  int guard = 0;
  while (uiFlags.firstStart && ++guard < 2000000) loop();
}

extern uint32_t rtcSavedEvNext;
bool saveEventLog(const char* reason);

static uint16_t fajlCount() {
  auto it = g_fs.find("/evlog.bin");
  if (it == g_fs.end() || it->second.size() < 16) return 0;
  uint16_t c;
  memcpy(&c, it->second.data() + 6, sizeof(c));
  return c;
}

static void scNV1() {
  {
    coldBoot(false, "", "", "", "");
    setup();
    CHECK(deviceMode == (DeviceMode)1, "AP modba valtottunk");
    CHECK(g_fs.count("/evlog.bin") == 1, "es a naplo kiment a fajlrendszerre");
    CHECK(fajlCount() > 0, "van benne bejegyzes");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    g_httpBody = "Rossz"; pingSim.ok = false;
    setup();
    g_fs.erase("/evlog.bin");
    int guard = 0;
    try {
      while (g_fs.count("/evlog.bin") == 0 && ++guard < 300000) loop();
    } catch (DeepSleepSignal&) {}
    CHECK(g_fs.count("/evlog.bin") == 1, "a router reset elott is kiment");
    CHECK(serialIndex("Naplo mentese") < serialIndex("Router resetting"),
          "es tenyleg a rele kapcsolasa ELOTT");
  }

  {
    coldBoot(false, "TestNet", "pw", "", "");
    setup();
    int guard = 0;
    bool slept = false;
    try { while (!slept && ++guard < 900000) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    CHECK(slept, "eljutottunk az alvasig");
    CHECK(serialHas("alvas elott"), "az alvas elott is mentettunk");
    CHECK(logIndex("Naplo mentese") < logIndex("DEEP_SLEEP")
          || serialIndex("alvas elott") >= 0, "a mentes az elalvas elott tortent");
  }
}

static void scNV9() {
  {
    coldBoot(false, "", "", "", "");
    setup();
    g_fs.erase("/evlog.bin");
    bool slept = false;
    int guard = 0;
    try { while (!slept && ++guard < 4000000) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    CHECK(slept, "az AP hatarido lejartaval elalszik");
    CHECK(g_fs.count("/evlog.bin") == 1, "apSleep: a naplo kiment a fajlba");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    setup();
    monitorUzembe();
    g_fs.erase("/evlog.bin");
    enterFatal("teszt");
    bool slept = false;
    int guard = 0;
    try { while (!slept && ++guard < 4000000) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    CHECK(slept, "5 perc hibajelzes utan elalszik");
    CHECK(g_fs.count("/evlog.bin") == 1, "fatalSleep: a naplo kiment a fajlba");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    g_fsMountOk = false;
    setup();
    CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL a csatolasi hiba miatt");
    bool slept = false;
    int guard = 0;
    try { while (!slept && ++guard < 4000000) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    CHECK(slept, "fajlrendszer nelkul is elalszik, nem akad meg a mentesen");
    CHECK(g_fs.count("/evlog.bin") == 0, "es nem is probal irni");
  }
}

static void scNV10() {
  {
    coldBoot(true, "TestNet", "pw", "", "");
    wifiSim.willConnect = false; pingSim.ok = false;
    setup();
    const int startsSetupUtan = g_ntpStarts;

    wifiSim.willConnect = true; pingSim.ok = true;
    g_httpBody = "Microsoft Connect Test";
    int guard = 0;
    try { while (uiFlags.firstStart && ++guard < 2000000) loop(); }
    catch (DeepSleepSignal&) {}
    CHECK(!uiFlags.firstStart, "a firstStart a proba korai kilepesevel zarult");
    CHECK(serialHas("halozat es internet visszajott"), "tenyleg a korai agon");
    CHECK(g_ntpStarts > startsSetupUtan,
          "es az oraszinkron ELINDULT ezen az uton is");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    g_httpBody = "Microsoft Connect Test";
    setup();
    int guard = 0;
    while (uiFlags.firstStart && ++guard < 2000000) loop();
    loop();
    const int elotte = g_ntpStarts;
    CHECK(elotte >= 1, "el a szinkron");

    wifiSim.willConnect = false;
    loop();

    CHECK(g_ntpStarts == elotte, "kapcsolat nelkul nem indul ujabb szinkron");
    wifiSim.willConnect = true;
    wifiSim.begun = true; wifiSim.beginAt = 0;
    loop();
    CHECK(g_ntpStarts > elotte, "ujracsatlakozas utan ujra elindul a szinkron");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    g_httpBody = "Microsoft Connect Test";
    setup();
    int guard = 0;
    while (uiFlags.firstStart && ++guard < 2000000) loop();
    loop();
    const int elotte = g_ntpStarts;
    for (int i = 0; i < 500; i++) loop();
    CHECK(g_ntpStarts == elotte, "elo kapcsolat mellett nem inditgatjuk ujra");
  }
}

static void scNV11() {
  coldBoot(false, "TestNet", "pw", "", "");
  g_epochNow = 0;
  setup();

  bool slept = false;
  int guard = 0;
  try { while (!slept && ++guard < 900000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "az egesz eszkalacio lefutott oraszinkron nelkul is");
  CHECK(g_fs.count("/evlog.bin") == 1, "a naplo mentese is sikerult");
  CHECK(!serialHas("1970"), "sehol nem jelenik meg hamis 1970-es datum");

  const std::string mentett = g_fs["/evlog.bin"];
  CHECK(!mentett.empty(), "a mentett naplo tartalma megvan");
  coldBoot(false, "", "", "", "");
  g_epochNow = 0;
  setup();
  g_fs["/evlog.bin"] = mentett;
  rtcEvMagic = 0; rtcEvNext = 0;

  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  CHECK(req._code == 200, "a /log oldal ora nelkul is kiszolgalodik");
  CHECK(b.find("fajlrendszerre mentett naplo") != std::string::npos,
        "a frissesseg-dontes a darabszam-alapu szabalyra esett vissza");
  CHECK(b.find("<td>-</td>") != std::string::npos,
        "az Ido oszlopban '-' all, nem hamis datum");
  CHECK(b.find("1970") == std::string::npos, "sehol nincs 1970-es datum");
  CHECK(b.find("<table") != std::string::npos, "es a tabla ott van");

  CHECK(b.find("(mentve:") == std::string::npos,
        "ora nelkul nem ir ki datumot");
  CHECK(b.find("(utoljara mentve a bootolas utan") != std::string::npos,
        "hanem a mentes akkori uptime-jat");
}

static void scNV12() {
  coldBoot(false, "", "", "", "");

  g_fsCapacity = 24;
  setup();
  CHECK(serialHas("rovid iras") || serialHas("NEM sikerult"),
        "a rovid irast eszrevesszuk");
  CHECK(rtcSavedEvNext == 0, "es NEM jegyezzuk be sikeres mentesnek");
  CHECK(deviceMode == (DeviceMode)1, "a mukodes zavartalanul folytatodik");
  CHECK(rtcEvNext > 0, "az RTC naplo tovabbra is ep");

  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  CHECK(req._code == 200, "a /log oldal ettol meg kiszolgalodik");
  CHECK(req._body.find("nincs olvashato naplofajl") != std::string::npos,
        "a fel-kesz fajlt elutasitja");
  CHECK(req._body.find("RTC memoriabol") != std::string::npos,
        "es az ep RTC naplot mutatja");
}

static void scNV13() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();
  g_fs.erase("/evlog.bin");
  rtcSavedEvNext = 0;

  CHECK(saveEventLog("elso"), "az elso mentes lefut");
  const std::string elso = g_fs["/evlog.bin"];
  CHECK(!elso.empty(), "es tenyleg irt is");
  const uint32_t mentettPozicio = rtcSavedEvNext;

  CHECK(!saveEventLog("masodik, valtozas nelkul"),
        "valtozas nelkul a mentes kimarad");
  CHECK(rtcSavedEvNext == mentettPozicio, "a jelzo sem mozdul");
  CHECK(!configWriteInProgress(), "es a zarat sem szerezte meg feleslegesen");

  logEvent((EventCode)2, 77);
  CHECK(saveEventLog("harmadik, uj esemeny utan"),
        "uj esemeny utan viszont ujra ment");
  CHECK(rtcSavedEvNext > mentettPozicio, "es a jelzo is elorelep");
}

static void scNV14() {
  coldBoot(false, "", "", "", "");
  g_epochNow = 1780000000UL;
  setup();
  CHECK(g_fs.count("/evlog.bin") == 1, "van mentett naplo");

  rtcEvMagic = 0; rtcEvNext = 0;
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  CHECK(b.find("fajlrendszerre mentett naplo") != std::string::npos,
        "a fajlbol dolgozik");
  CHECK(b.find("(utoljara mentve: 2026-") != std::string::npos,
        "es kiirja, MIKOR mentettuk");
}

static void scNV15() {
  struct Eset { esp_reset_reason_t ok; const char* resz; };
  const Eset esetek[] = {
    { ESP_RST_POWERON,    "bekapcsolas" },
    { ESP_RST_EXT,        "kulso reset" },
    { ESP_RST_SW,         "szoftveres" },
    { ESP_RST_PANIC,      "PANIC" },
    { ESP_RST_INT_WDT,    "megszakitas-watchdog" },
    { ESP_RST_TASK_WDT,   "TASK WATCHDOG" },
    { ESP_RST_WDT,        "egyeb watchdog" },
    { ESP_RST_DEEPSLEEP,  "deep sleep" },
    { ESP_RST_BROWNOUT,   "BROWNOUT" },
    { ESP_RST_CPU_LOCKUP, "CPU lefagyas" },
    { ESP_RST_SDIO,       "SDIO" },
    { ESP_RST_USB,        "USB reset" },
    { ESP_RST_JTAG,       "JTAG reset" },
    { ESP_RST_EFUSE,      "eFuse" },
    { ESP_RST_PWR_GLITCH, "TAPFESZULTSEG-TUSKE" },
  };
  bool mind = true;
  for (const Eset& e : esetek) {
    coldBoot(false, "", "", "", "");
    g_resetReason = e.ok;
    rtcWdtMagic = 0;
    setup();
    AsyncWebServerRequest req; g_handlers["/log#1"](&req);
    if (req._body.find(e.resz) == std::string::npos) {
      mind = false;
      printf("     [info] hianyzik a(z) %d okhoz: '%s'\n", (int)e.ok, e.resz);
    }
  }
  CHECK(mind, "mind a 15 nevesitett indulasi ok EMBERI nevet kap a /log oldalon");

  coldBoot(false, "", "", "", "");
  g_resetReason = ESP_RST_UNKNOWN;
  setup();
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  CHECK(req._body.find("ismeretlen") != std::string::npos,
        "ismeretlen ok eseten sem hallgat el a lap");
}

static void scAP7() {
  coldBoot(false, "", "", "", "");
  setup();

  std::string ssid32(32, 'A');
  std::string pass63(63, 'b');
  const int kod = postConfig((ssid32 + " ").c_str(), (pass63 + " ").c_str(),
                             " 192.168.1.200 ", " 192.168.1.1 ");
  CHECK(kod == 200, "a hatarertekes ertekek zaro szokozzel is mentodnek");
  CHECK(g_fs["/ssid.txt"] == ssid32, "az SSID a VAGOTT, 32 karakteres ertek");
  CHECK(storedPass() == pass63, "a jelszo a VAGOTT, 63 karakteres ertek");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200", "az IP is vagva");
  CHECK(g_fs["/gateway.txt"] == "192.168.1.1", "es a gateway is");

  clearRestartRequest(); endConfigWrite();
  std::string ssid33(33, 'C');
  CHECK(postConfig(ssid33.c_str(), "jelszo123", "", "") == 500,
        "33 karakteres SSID viszont tovabbra is hiba");
  CHECK(g_fs["/ssid.txt"] == ssid32, "es a korabbi ertek erintetlen maradt");
}

static void scAP5() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("O'Brien & <Wifi> \"haz\"", "jelszo123", "", "") == 200,
        "mentes kulonleges karakterekkel");
  clearRestartRequest();

  AsyncWebServerRequest req; g_handlers["/#1"](&req);
  const std::string& b = req._body;
  CHECK(b.find("&#39;") != std::string::npos, "az aposztrof escape-elve");
  CHECK(b.find("&amp;") != std::string::npos, "az &-jel escape-elve");
  CHECK(b.find("&lt;Wifi&gt;") != std::string::npos, "a < es > escape-elve");
  CHECK(b.find("&quot;") != std::string::npos, "az idezojel escape-elve");

  CHECK(b.find("O'Brien") == std::string::npos, "nyers aposztrof nincs a lapon");
  CHECK(b.find("<Wifi>") == std::string::npos, "nyers < > nincs a lapon");
}

static void scNV16() {
  {
    coldBoot(false, "", "", "", "");
    g_fsReadable = false;
    setup();
    CHECK(serialHas("verify") || serialHas("NEM sikerult"),
          "a visszaolvasas kudarcat eszrevesszuk");
    CHECK(rtcSavedEvNext == 0, "es nem konyveljuk el sikeres mentesnek");
    CHECK(deviceMode == (DeviceMode)1, "a mukodes ettol meg folytatodik");
  }

  {
    coldBoot(false, "", "", "", "");
    setup();
    CHECK(g_fs.count("/evlog.bin") == 1, "van mentett naplo");
    logEvent((EventCode)5, 555);
    g_fsReadable = false;
    AsyncWebServerRequest req; g_handlers["/log#1"](&req);
    g_fsReadable = true;
    CHECK(req._code == 200, "a /log ettol meg kiszolgalodik");
    CHECK(req._body.find("<td>555</td>") != std::string::npos,
          "es az RTC naplot mutatja");
  }

  {
    coldBoot(false, "", "", "", "");
    setup();
    rtcEvMagic = 0; rtcEvNext = 0;
    logEvent((EventCode)5, 666);
    g_fsShortRead = true; g_fsShortReadSkip = 1;
    AsyncWebServerRequest req; g_handlers["/log#1"](&req);
    g_fsShortRead = false; g_fsShortReadSkip = 0;
    CHECK(req._code == 200, "a lap itt is kiszolgalodik");
    CHECK(req._body.find("<td>666</td>") != std::string::npos,
          "es az RTC bejegyzes ep egeszben latszik");
  }
}

static void scAP6() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP mod, fut a portal");
  setFilesystemReady(false);
  const int kod = postConfig("Halozat", "jelszo123", "", "");
  CHECK(kod == 500, "csatolatlan fajlrendszernel 500-at ad");
  CHECK(g_fs.find("/ssid.txt") == g_fs.end(), "es tenyleg semmit nem irt ki");
  CHECK(!restartRequested(), "ujraindulast sem utemez be");
}

static void scWF11() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Rossz"; pingSim.ok = false;
  setup();

  int guard = 0;
  try {
    while (currentState != (State)2 && ++guard < 900000) loop();
  } catch (DeepSleepSignal&) {}
  CHECK(currentState == (State)2, "megtortent a router reset");
  wifiSim.willConnect = false;

  bool slept = false;
  guard = 0;
  try { while (!slept && ++guard < 900000) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(serialHas("A router reset utan sem jott vissza a WiFi."),
        "a hibaag lefutott es meg is mondja");
  CHECK(slept || deviceMode == (DeviceMode)1,
        "es a wifiGiveUp() dontott: alvas vagy AP mod");
}

static void scNV2() {
  coldBoot(false, "", "", "", "");
  g_fsSilentWriteFail = true;
  setup();
  CHECK(serialHas("verify FAILED") || serialHas("NEM sikerult"),
        "a nema irasi hibat eszrevesszuk");
  CHECK(rtcSavedEvNext == 0, "es NEM jegyezzuk be sikeres mentesnek");

  CHECK(deviceMode == (DeviceMode)1, "a mukodes zavartalanul folytatodik");
  CHECK(rtcEvNext > 0, "es az RTC naplo tovabbra is ep");
}

static void scNV3() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();
  g_fs.erase("/evlog.bin");

  (void)beginConfigWrite();
  const bool ok = saveEventLog("teszt");
  CHECK(!ok, "foglalt zarnal a mentes kimarad");
  CHECK(g_fs.count("/evlog.bin") == 0, "es tenyleg nem is irt semmit");
  CHECK(configWriteInProgress(), "a mas altal tartott zarat nem engedte el");
  CHECK(serialHas("kimarad"), "es meg is mondja, miert");

  endConfigWrite();
  const bool ok2 = saveEventLog("teszt");
  CHECK(ok2, "szabad zarnal viszont lefut");
  CHECK(!configWriteInProgress(), "es a mentes UTAN feloldja a zarat");
}

static void scNV4() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();

  (void)beginConfigWrite();
  g_pinRead[PIN_RESETBTN] = LOW;
  bool restarted = false;
  try {
    for (int i = 0; i < 100; i++) { resetbutton(); g_millis += 10; }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "mentes kozben a reset gomb nem indit ujra");
  g_pinRead[PIN_RESETBTN] = HIGH;

  CHECK(!beginConfigWrite(), "es masik fajliras sem indulhat");
  endConfigWrite();
}

static void scNV5() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(g_fs.count("/evlog.bin") == 1, "van mentett naplo");

  for (int i = 0; i < 10; i++) logEvent((EventCode)2, (uint16_t)(900 + i));
  {
    AsyncWebServerRequest req; g_handlers["/log#1"](&req);
    const std::string& b = req._body;
    CHECK(b.find("fajlrendszerre mentett naplo") != std::string::npos,
          "a lap a mentett fajlt is mutatja");
    CHECK(b.find("az RTC memoriabol 10, a mentes ota") != std::string::npos,
          "es mellette a mentes ota keletkezett 10 bejegyzest");
    CHECK(b.find("<td>909</td>") != std::string::npos,
          "a friss bejegyzes tenyleg ott van");

    size_t fajlSor = 0, rtcSor = 0, pos = 0;
    while ((pos = b.find("<td>fajl</td>", pos)) != std::string::npos) { fajlSor++; pos++; }
    pos = 0;
    while ((pos = b.find("<td>RTC</td>", pos)) != std::string::npos) { rtcSor++; pos++; }
    printf("     [info] %zu sor a fajlbol, %zu az RTC-bol\n", fajlSor, rtcSor);
    CHECK(rtcSor == 10, "az RTC reszbol PONTOSAN a 10 uj sor latszik");
    CHECK(fajlSor > 0, "a fajl resze sem ures");
  }

  const uint16_t elozoDb = fajlCount();
  rtcEvMagic = 0; rtcEvNext = 0;
  {
    AsyncWebServerRequest req; g_handlers["/log#1"](&req);
    CHECK(req._body.find("fajlrendszerre mentett naplo") != std::string::npos,
          "torolt RTC naplonal is megvan a fajl tortenete");
    CHECK(req._body.find("az RTC memoriabol 0,") != std::string::npos,
          "es az RTC reszbol nulla sor - nincs mit mutatni");
    CHECK(req._body.find("<table") != std::string::npos,
          "es van is mit mutatnia");
    CHECK(elozoDb > 0, "a fajlban tenyleg volt bejegyzes");
  }
}

static void scNV6() {
  const char* esetek[] = { "nincs fajl", "ures fajl", "csonka fajl",
                           "rossz magic", "hazudos fejlec" };
  for (int e = 0; e < 5; e++) {
    coldBoot(false, "", "", "", "");
    setup();
    logEvent((EventCode)2, (uint16_t)(700 + e));
    switch (e) {
      case 0: g_fs.erase("/evlog.bin"); break;
      case 1: g_fs["/evlog.bin"] = ""; break;
      case 2: g_fs["/evlog.bin"] = std::string("BAZF12", 6); break;
      case 3: { std::string b = g_fs["/evlog.bin"]; if (b.size() > 4) b[0] = 'X';
                g_fs["/evlog.bin"] = b; break; }
      case 4: {
                std::string b = g_fs["/evlog.bin"];
                if (b.size() >= 8) { uint16_t c = 200; memcpy(&b[6], &c, 2); }
                g_fs["/evlog.bin"] = b; break; }
    }
    AsyncWebServerRequest req; g_handlers["/log#1"](&req);
    char msg[96];
    snprintf(msg, sizeof(msg), "%s: a lap az RTC naplot mutatja, hiba nelkul", esetek[e]);
    CHECK(req._code == 200
          && req._body.find("nincs olvashato naplofajl") != std::string::npos, msg);
  }

  coldBoot(false, "", "", "", "");
  setup();
  rtcEvMagic = 0; rtcEvNext = 0;
  g_fs.erase("/evlog.bin");
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  CHECK(req._body.find("Nincs rogzitett esemeny") != std::string::npos,
        "mindketto ures: egyszeruen nincs naplo a lapon");
  CHECK(req._body.find("<table") == std::string::npos, "es nincs ures tablazat sem");
}

static void scNV7() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_epochNow = 0;
  setup();

  loop();
  CHECK(g_ntpStarts >= 1, "sikeres csatlakozas utan elindul az oraszinkron");

  logEvent((EventCode)2, 111);
  g_epochNow = 1780000000UL;
  logEvent((EventCode)2, 222);

  startConfigPortal();
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  CHECK(b.find("2026-") != std::string::npos,
        "a szinkron utani bejegyzes valos idot mutat");
  CHECK(b.find("<td>-</td>") != std::string::npos,
        "a szinkron elottinel '-' all az ido oszlopban, nem hamis datum");
  CHECK(b.find("<th>Ido</th>") != std::string::npos, "van Ido oszlop");

  CHECK(rtcEvents[(rtcEvNext - 1) % 32].epoch == 1780000000UL,
        "az epoch tenyleg bekerult a bejegyzesbe");
}

static void scNV8() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(g_fs.count("/evlog.bin") == 1, "van mentett naplo");

  rtcEvMagic = 0; rtcEvNext = 0;
  logEvent((EventCode)5, 4242);
  g_fsShortRead = true; g_fsShortReadSkip = 2;
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  g_fsShortRead = false; g_fsShortReadSkip = 0;

  CHECK(req._code == 200, "a lap ettol meg kiszolgalodik");
  CHECK(req._body.find("<td>4242</td>") != std::string::npos,
        "es az RTC bejegyzes EP EGESZBEN latszik (nem kevert puffer)");
  CHECK(req._body.find("naplofajl olvasasa megszakadt") != std::string::npos
        || req._body.find("nincs olvashato naplofajl") != std::string::npos,
        "a forras helyesen az RTC naplo");
}

extern uint32_t rtcHeapMagic;
extern uint32_t rtcHeapRestarts;
extern uint8_t  rtcCarryResetEvents;
extern uint32_t rtcCarryRetryRounds;

static void scHP1() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  const size_t before = g_serialLog.size();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 3u * 3600 * 1000 && ++guard < 3000000) loop(); }
  catch (DeepSleepSignal&) {} catch (RestartSignal&) {}

  int heapSorok = 0;
  for (size_t i = before; i < g_serialLog.size(); i++)
    if (g_serialLog[i].find("Heap: szabad") != std::string::npos) heapSorok++;
  const uint32_t orak = (g_millis - t0) / 3600000;
  printf("     [info] %u ora alatt %d heap-allapotsor (%.1f oranként)\n",
         orak, heapSorok, heapSorok ? orak / (double)heapSorok : 0.0);
  CHECK(heapSorok >= 2, "rendszeresen ki is irja az allapotot");
  CHECK(heapSorok <= (int)(orak * 2 + 2), "de felorankent legfeljebb egyszer");
  CHECK(!serialHas("FIGYELEM: alacsony"), "egeszseges heapnel nem figyelmeztet");
}

static void hp10Hook() {
  const bool alacsony = ((g_millis / 20000) % 2) == 0;
  g_freeHeap = alacsony ? 20000 : 40000;
  g_maxAllocHeap = g_freeHeap;
}

static void scHP10() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  int guard = 0;
  while (uiFlags.firstStart && ++guard < 2000000) loop();

  rtcEvMagic = 0; rtcEvNext = 0;
  g_onDelay = hp10Hook;
  const uint32_t t0 = g_millis;
  guard = 0;
  try { while (g_millis - t0 < 30u * 60 * 1000 && ++guard < 2000000) loop(); }
  catch (DeepSleepSignal&) {} catch (RestartSignal&) {}
  g_onDelay = nullptr;

  int lowHeap = 0, egymasUtan = 0, maxEgymasUtan = 0;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 13) {
      lowHeap++; egymasUtan++;
      if (egymasUtan > maxEgymasUtan) maxEgymasUtan = egymasUtan;
    } else {
      egymasUtan = 0;
    }
  }
  printf("     [info] 30 perc ingadozas: %d LOW HEAP bejegyzes, max %d egymas utan\n",
         lowHeap, maxEgymasUtan);
  CHECK(maxEgymasUtan <= 1, "nincs ket EGYMAS UTANI LOW HEAP a naplóban");
  CHECK(lowHeap >= 1, "de az elso alkalommal tenyleg naplozunk");
  CHECK(rtcEvNext < 32, "a korpuffer nem fordult korbe a figyelmeztetesektol");
}

static void scHP2() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  g_freeHeap = 20000; g_minFreeHeap = 20000; g_maxAllocHeap = 18000;
  setup();
  monitorUzembe();
  const size_t before = g_serialLog.size();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 10u * 60 * 1000 && ++guard < 2000000) loop(); }
  catch (DeepSleepSignal&) {} catch (RestartSignal&) {}

  int figy = 0;
  for (size_t i = before; i < g_serialLog.size(); i++)
    if (g_serialLog[i].find("FIGYELEM: alacsony") != std::string::npos) figy++;
  printf("     [info] 10 perc alatt %d figyelmeztetes (meres 10 mp-enkent)\n", figy);
  CHECK(figy <= 1, "a figyelmeztetes 10 perc alatt is legfeljebb egyszer szol");
  CHECK(serialHas("FIGYELEM: alacsony"), "de egyszer igen");

  int naplo = 0;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++)
    if (rtcEvents[i].code == 13) naplo++;
  CHECK(naplo == 1, "a naploba is pontosan egy LOW HEAP kerult");
}

static void scHP3() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();

  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false;
  int guard = 0;
  try {
    for (int i = 0; i < 3 && !restarted; i++) {
      const uint32_t t = g_millis;
      while (g_millis - t < 11u * 1000 && ++guard < 200000) loop();
      if (i == 0) { g_freeHeap = 180000; g_maxAllocHeap = 110000; }
    }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "egyetlen melypont NEM indit ujra");

  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  guard = 0;
  const uint32_t t0 = g_millis;
  try { while (!restarted && ++guard < 200000) loop(); }
  catch (RestartSignal&) { restarted = true; }
  const uint32_t eltelt = g_millis - t0;
  printf("     [info] tartos kritikus szint utan %u mp-cel indult ujra\n",
         eltelt / 1000);
  CHECK(restarted, "tartosan kritikus szintnel viszont ujraindul");
  CHECK(eltelt >= 20000, "de csak tobb egymast koveto meres utan (nem azonnal)");
  CHECK(serialHas("Onkentes ujraindulas"), "es meg is mondja, miert");
}

static void scHP4() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  testState.resetEvents = 3;

  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false;
  int guard = 0;
  try {
    while (!restarted && ++guard < 200000) { checkHeap(g_millis); g_millis += 1000; }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a heap miatt ujraindult");
  CHECK(rtcCarryResetEvents == 3, "az atvitel rogzitette a router reset szamlalot");
  CHECK(rtcHeapRestarts == 1, "es szamon tartja, hanyadik heap-ujraindulas volt");

  const uint32_t heapMagic = rtcHeapMagic;
  const uint32_t heapRestarts = rtcHeapRestarts;
  const uint8_t carry = rtcCarryResetEvents;
  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  rtcHeapMagic = heapMagic; rtcHeapRestarts = heapRestarts;
  rtcCarryResetEvents = carry;
  g_freeHeap = 180000; g_minFreeHeap = 180000; g_maxAllocHeap = 110000;
  setup();
  CHECK(testState.resetEvents == 3,
        "az ujraindulas utan a router reset szamlalo FOLYTATODIK, nem nullazodik");
  CHECK(rtcCarryResetEvents == 0xFF,
        "az atvitelt pontosan EGYSZER hasznaljuk fel");
  CHECK(serialHas("folytatjuk: router reset szamlalo"), "es ezt ki is irja");

  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  rtcHeapMagic = heapMagic; rtcCarryResetEvents = 0xFF;
  setup();
  CHECK(testState.resetEvents == 0,
        "gombos/watchdog ujraindulas utan viszont tiszta lappal indul");
}

static void scHP5() {
  {
    coldBoot(false, "", "", "", "");
    rtcHeapMagic = 0;
    setup();
    CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk");
    g_freeHeap = 5000; g_maxAllocHeap = 4000;
    bool restarted = false, slept = false;
    int guard = 0;
    try {
      const uint32_t t = g_millis;
      while (g_millis - t < 3u * 60 * 1000 && ++guard < 500000) loop();
    } catch (RestartSignal&) { restarted = true; } catch (DeepSleepSignal&) { slept = true; }
    CHECK(!restarted, "AP modban NEM indit ujra (elveszne a beirt konfig)");
    CHECK(!slept, "es meg nem is alszik el - fut a portal");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    rtcHeapMagic = 0;
    setup();
    monitorUzembe();
    testState.resetStep = 1;
    timing.resetPulseStart = g_millis;
    digitalWrite(10, HIGH);
    g_freeHeap = 5000; g_maxAllocHeap = 4000;
    bool restarted = false;
    int guard = 0;
    try {
      const uint32_t t = g_millis;
      while (g_millis - t < 60u * 1000 && ++guard < 200000) {
        checkHeap(g_millis); g_millis += 1000;
      }
    } catch (RestartSignal&) { restarted = true; }
    CHECK(!restarted, "a rele impulzusa alatt NEM indit ujra");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    rtcHeapMagic = 0;
    setup();
    monitorUzembe();
    (void)beginConfigWrite();
    g_freeHeap = 5000; g_maxAllocHeap = 4000;
    bool restarted = false;
    int guard = 0;
    try {
      const uint32_t t = g_millis;
      while (g_millis - t < 60u * 1000 && ++guard < 200000) {
        checkHeap(g_millis); g_millis += 1000;
      }
    } catch (RestartSignal&) { restarted = true; }
    CHECK(!restarted, "fajliras kozben sem indit ujra");
    endConfigWrite();
  }
}

static void scHP6() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  rtcHeapRestarts = 3;
  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false;
  int guard = 0;
  try {
    const uint32_t t = g_millis;
    while (g_millis - t < 60u * 1000 && ++guard < 200000) loop();
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "a negyedik korben mar NEM indul ujra");
  CHECK(deviceMode == (DeviceMode)2, "hanem vegzetes hibat jelez");
  bool found = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++)
    if (rtcEvents[i].code == 9 && rtcEvents[i].param == 5) found = true;
  CHECK(found, "a naploban FATAL(5) - tartosan keves a heap");
}

static void scHP7() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  rtcHeapRestarts = 2;
  g_millis = timing.startMillis + 59u * 60 * 1000;
  loop();
  CHECK(rtcHeapRestarts == 2, "egy oran belul meg nem nullaz");
  g_millis = timing.startMillis + 61u * 60 * 1000;
  loop();
  CHECK(rtcHeapRestarts == 0, "egy ora hibatlan mukodes utan viszont igen");
  CHECK(serialHas("heap ujrainditas szamlalo nullazva"), "es kiirja");
}

static void scHP8() {
  coldBoot(false, "", "", "", "");
  rtcHeapMagic = 0;
  setup();
  logEvent((EventCode)13, 24);
  logEvent((EventCode)14, 11);
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  CHECK(b.find(">LOW HEAP<") != std::string::npos, "a LOW HEAP nevvel jelenik meg");
  CHECK(b.find(">HEAP RESTART<") != std::string::npos, "es a HEAP RESTART is");
  CHECK(b.find("LOW HEAP: a szabad heap KB-ban") != std::string::npos,
        "a jelmagyarazat megfejti a parametert");
  CHECK(b.find("5 = tartosan keves heap") != std::string::npos,
        "es a FATAL uj oka is szerepel benne");
}

static void scHP9() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  testState.resetEvents = 2;

  g_heapDrainPerCall = 400;
  bool restarted = false;
  int guard = 0;
  const uint32_t t0 = g_millis;
  try {
    while (!restarted && ++guard < 300000) { checkHeap(g_millis); g_millis += 1000; }
  } catch (RestartSignal&) { restarted = true; }

  printf("     [info] a szivargas kezdetetol az onkentes ujraindulasig: %u perc\n",
         (g_millis - t0) / 60000);
  CHECK(restarted, "a szivargas vegul onkentes ujraindulashoz vezetett");

  const int figy = serialIndex("FIGYELEM: alacsony");
  const int ujra = serialIndex("Onkentes ujraindulas");
  CHECK(figy >= 0, "elobb figyelmeztetett");
  CHECK(ujra > figy, "es csak azutan indult ujra");

  bool low = false, hres = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 13) low = true;
    if (rtcEvents[i].code == 14) hres = true;
  }
  CHECK(low && hres, "a naploban LOW HEAP es HEAP RESTART is szerepel");
  CHECK(rtcCarryResetEvents == 2, "es a router reset szamlalo atvitele is megvan");
}

extern uint32_t rtcHeapMagic;
extern uint32_t rtcHeapRestarts;
extern uint8_t  rtcCarryResetEvents;
extern uint32_t rtcCarryRetryRounds;

static void scLOG7() {
  coldBoot(false, "", "", "", "");
  g_epochNow = 1780000000UL;
  setup();

  for (int kor = 0; kor < 6; kor++) {
    for (int i = 0; i < EVLOG_SIZE_C; i++) {
      logEvent((EventCode)5, (uint16_t)(60000 + i));
    }
    saveEventLog("LOG7 toltes");
  }

  for (int i = 0; i < EVLOG_SIZE_C; i++) logEvent((EventCode)5, (uint16_t)(60000 + i));

  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const size_t meret = req._body.size();
  size_t sorok = 0, tol = 0, fajlSor = 0, rtcSor = 0, p2 = 0;
  while ((tol = req._body.find("<tr><td>", tol)) != std::string::npos) { sorok++; tol += 8; }
  while ((p2 = req._body.find("<td>fajl</td>", p2)) != std::string::npos) { fajlSor++; p2++; }
  p2 = 0;
  while ((p2 = req._body.find("<td>RTC</td>", p2)) != std::string::npos) { rtcSor++; p2++; }
  printf("     [info] a /log legrosszabb esetben %u bajt, %zu sor "
         "(%zu fajl + %zu RTC)\n", (unsigned)meret, sorok, fajlSor, rtcSor);
  CHECK(req._code == 200, "a lap kiszolgalodik");
  CHECK(rtcSor == EVLOG_SIZE_C, "mind a 32 RTC-sor ott van");
  CHECK(fajlSor == EVFILE_SIZE_C, "es mind a 128 fajl-sor is - nincs tobbe ablak");
  CHECK(sorok == EVFILE_SIZE_C + EVLOG_SIZE_C, "osszesen 160 sor");

  const size_t becsult = 2600 + sorok * 120;
  printf("     [info] becsult puffer %zu bajt, tenyleges lap %u bajt\n",
         becsult, (unsigned)meret);
  CHECK(meret < becsult, "a becsult puffer fedezi a tenyleges lapot");
}

static void scLOG8() {
  coldBoot(false, "", "", "", "");
  rtcEvMagic = 0x42415A4CUL;
  rtcEvNext = 17;
  for (int i = 0; i < 32; i++) {
    rtcEvents[i].uptimeSec = 0xDEADBEEF;
    rtcEvents[i].epoch = 0xDEADBEEF;
    rtcEvents[i].code = 200;
    rtcEvents[i].param = 9999;
  }
  setup();
  CHECK(rtcEvNext < 17, "a regi magic ervenytelen: a naplo tiszta lappal indult");
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  CHECK(req._body.find("<td>9999</td>") == std::string::npos,
        "a regi elrendezes szemete NEM jelenik meg a lapon");
  CHECK(req._body.find(">?<") == std::string::npos,
        "es ismeretlen esemenykod sincs");
}

static void scLOG9() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(g_fs.count("/evlog.bin") == 1, "van mentett naplo");
  CHECK(postConfig("Halozat", "jelszo123", "", "") == 200, "mentettunk konfigot");
  clearRestartRequest(); endConfigWrite();

  bool restarted = false;
  try { doWifiReset(); } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a wifireset ujrainditott");
  CHECK(g_fs["/ssid.txt"].empty(), "az SSID torolve");
  CHECK(g_fs.count("/evlog.bin") == 1,
        "a naplofajl viszont MEGMARADT - epp most kell a diagnozishoz");
}

static void gatewayHiba() {
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "192.168.1.1");
  g_httpBody = "Rossz";
  pingSim.ok = false;
  pingSim.perTarget["192.168.1.1"] = false;
  setup();
}

static void scGWH1() {
  gatewayHiba();

  int guard = 0;
  try {
    while (currentState != (State)2 && ++guard < 900000) loop();
  } catch (DeepSleepSignal&) {} catch (RestartSignal&) {}
  CHECK(currentState == (State)2, "az elso fazis lefutott: a router megkapta az eselyet");
  CHECK(serialHas("A sajat gateway sem elerheto"), "es a gateway-hiba miatt");
  const uint8_t resetekAddig = testState.resetEvents;

  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false;
  guard = 0;
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 5u * 60 * 1000 && ++guard < 500000) loop();
  } catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) {}
  CHECK(!restarted,
        "a router reset UTANI ellenorzo ablakban NINCS onkentes ujraindulas");
  CHECK(testState.resetEvents == resetekAddig,
        "es igy a router sem kapott folosleges masodik aramtalanitast");
}

static void scGWH2() {
  gatewayHiba();

  int guard = 0;
  bool restarted = false, slept = false;
  try {
    while (currentState != (State)2 && ++guard < 900000) loop();
  } catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(currentState == (State)2, "az elso fazis lefutott");
  g_freeHeap = 5000; g_maxAllocHeap = 4000;

  guard = 0;
  try {
    while (deviceMode != (DeviceMode)1 && ++guard < 900000) loop();
  } catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) { slept = true; }

  CHECK(!restarted && !slept, "vegig eljutott ujraindulas es alvas nelkul");
  CHECK(deviceMode == (DeviceMode)1,
        "a masodik fazis dontese megszuletett: AP beallito mod");
  CHECK(serialHas("Valoszinuleg rossz a statikus IP"),
        "es a helyes indoklassal - a felhasznalo tudja, mit javitson");
  bool gw2 = false, ap4 = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    if (rtcEvents[i].code == 12 && rtcEvents[i].param == 2) gw2 = true;
    if (rtcEvents[i].code == 6 && rtcEvents[i].param == 4) ap4 = true;
  }
  CHECK(gw2 && ap4, "a naploban GW UNREACH(2) es AP MODE(4) is szerepel");
}

static void scGWH3() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  CHECK(currentState != (State)2, "monitor uzemben, nem a reset utani varakozasban");

  testState.resetEvents = 1;

  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false;
  int guard = 0;
  try {
    while (!restarted && ++guard < 200000) { checkHeap(g_millis); g_millis += 1000; }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "normal monitor uzemben az ujraindulas viszont megtortenik");
  CHECK(rtcCarryResetEvents == 1, "a router reset szamlalo atmegy");

  const uint32_t hm = rtcHeapMagic, hr = rtcHeapRestarts;
  const uint8_t carry = rtcCarryResetEvents;
  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  rtcHeapMagic = hm; rtcHeapRestarts = hr; rtcCarryResetEvents = carry;
  setup();
  CHECK(testState.resetEvents == 1, "a router reset szamlalo folytatodik");
  CHECK(testState.outage, "a kieses folytatodik, a lepcso az indulastol szamit");
  CHECK(testState.resetStep == 0, "es nincs felben maradt rele impulzus");
  CHECK(uiFlags.firstStart, "a firstStart varakozas ujra lefut (a proba koran zarja)");
}

static void scGWH4() {
  coldBoot(false, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  CHECK(deviceMode == (DeviceMode)0, "monitor modban maradt es ujraprobal");
  rtcRetryRounds = 30;
  testState.resetEvents = 1;

  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false;
  int guard = 0;
  try {
    while (!restarted && ++guard < 200000) { checkHeap(g_millis); g_millis += 1000; }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a heap miatt ujraindult");
  CHECK(rtcCarryRetryRounds == 30, "az atvitel rogzitette a 2 napos ablak allasat");

  const uint32_t hm = rtcHeapMagic, hr = rtcHeapRestarts, cr = rtcCarryRetryRounds;
  const uint8_t ce = rtcCarryResetEvents;
  coldBoot(false, "TestNet", "pw", "", "");
  CHECK(rtcRetryRounds == 0, "a szoftveres reset tenyleg nullazta volna");
  rtcHeapMagic = hm; rtcHeapRestarts = hr;
  rtcCarryResetEvents = ce; rtcCarryRetryRounds = cr;
  setup();
  CHECK(rtcRetryRounds == 30,
        "az atvitel utan viszont a 2 napos ablak FOLYTATODIK, nem kezdodik elolrol");
  CHECK(testState.resetEvents == 1, "es a router reset szamlalo is");
  CHECK(serialHas("ujraprobalkozasi kor = 30"), "a soros porton is lathato");

  CHECK(rtcRetryRounds < 33, "meg nem ertunk a hatarra");
}

static void scGWH5() {
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  monitorUzembe();
  rtcRetryRounds = 20;

  g_pinRead[PIN_RESETBTN] = LOW;
  bool restarted = false;
  try {
    for (int i = 0; i < 200; i++) { resetbutton(); g_millis += 10; }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a reset gomb ujrainditott");
  CHECK(rtcCarryResetEvents == 0xFF,
        "gombnyomasnal NINCS atvitel - a felhasznaloi beavatkozas tiszta lap");

  const uint32_t hm = rtcHeapMagic;
  const uint8_t ce = rtcCarryResetEvents;
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = hm; rtcCarryResetEvents = ce;
  setup();
  CHECK(rtcRetryRounds == 0,
        "gombnyomas utan a 2 napos ablak elolrol indul (szandekolt)");
}

static void scWDT14() {
  const uint32_t bazisok[] = { 1u, 40u * 24 * 3600 * 1000 };
  for (uint32_t bazis : bazisok) {
    coldBoot(true, "TestNet", "pw", "", "", 500, true);
    g_millis = bazis;
    rtcWdtMagic = 0;
    setup();
    rtcWdtResets = 2;
    const uint32_t indulas = timing.startMillis;

    g_millis = indulas + 59u * 60 * 1000;
    loop();
    CHECK(rtcWdtResets == 2, "egy oran BELUL meg nem nullaz");

    g_millis = indulas + 61u * 60 * 1000;
    loop();
    CHECK(rtcWdtResets == 0, "egy ora hibatlan mukodes utan viszont nullaz");
  }
}

static void scWDT15() {
  coldBoot(true, "TestNet", "pw", "", "");

  g_millis = 0xFFFF0000u;
  rtcWdtMagic = 0;
  setup();
  rtcWdtResets = 1;
  const uint32_t indulas = timing.startMillis;

  g_millis = indulas + 59u * 60 * 1000;
  loop();
  CHECK(rtcWdtResets == 1, "a fordulas utan sem nullaz korabban");
  g_millis = indulas + 61u * 60 * 1000;
  loop();
  CHECK(rtcWdtResets == 0, "de az egy ora letelte utan igen");
}

static void scCC1() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk, a portal fut");
  const int beginBefore = wifiSim.beginCount;

  int guard = 0;
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 4u * 60 * 1000 && ++guard < 200000) {
      loop();
      if (guard % 5000 == 0) {
        postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1");
        clearRestartRequest();
        endConfigWrite();
      }
    }
  } catch (DeepSleepSignal&) {} catch (RestartSignal&) {}

  CHECK(wifiSim.beginCount == beginBefore,
        "a portal futasa alatt a loop EGYETLEN WiFi.begin()-t sem adott ki");
  CHECK(g_fs["/ssid.txt"] == "Halozat", "kozben viszont tenylegesen mentettunk");

  CHECK(deviceMode == (DeviceMode)1, "a mod nem valtott vissza monitorra");
}

static void scCC2() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP mod");
  g_fs["/ssid.txt"] = "Valami";
  g_fsRemoveOk = false;
  g_fsWritable = false;

  const int beginBefore = wifiSim.beginCount;

  g_pinRead[PIN_WIFIBTN] = LOW;
  bool slept = false;
  try {
    for (int i = 0; i < 200000 && !slept; i++) { wifiresetbutton(); g_millis += 10; }
  } catch (DeepSleepSignal&) { slept = true; } catch (RestartSignal&) {}
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL lett a sikertelen torlestol");
  CHECK(wifiSim.beginCount == beginBefore,
        "a vegzetes hiba again sem olvassa a loop a konfiguracios puffereket");
}

static void scCC3() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("ElsoHalozat", "jelszo123", "", "") == 200, "elso mentes");
  clearRestartRequest();

  AsyncWebServerRequest r1; g_handlers["/#1"](&r1);
  CHECK(r1._body.find("value=\"ElsoHalozat\"") != std::string::npos,
        "a GET urlap a TELJES, epp ervenyes SSID-t mutatja");

  CHECK(postConfig("MasodikHalozat", "masikjelszo", "", "") == 200, "masodik mentes");
  clearRestartRequest();
  AsyncWebServerRequest r2; g_handlers["/#1"](&r2);
  CHECK(r2._body.find("value=\"MasodikHalozat\"") != std::string::npos,
        "a kovetkezo GET mar a TELJES uj erteket - nincs felig atirt allapot");
  CHECK(r2._body.find("ElsoHalozat") == std::string::npos,
        "a regibol semmi nem maradt benne");
}

static void gombNyomasPattogva(int pin, uint32_t tartasMs, int pattanas = 4) {
  for (int i = 0; i < pattanas; i++) {
    g_pinRead[pin] = LOW;  simIsr(pin);  g_millis += 1;
    g_pinRead[pin] = HIGH; simIsr(pin);  g_millis += 1;
  }
  g_pinRead[pin] = LOW; simIsr(pin);
  g_millis += tartasMs;
  for (int i = 0; i < pattanas; i++) {
    g_pinRead[pin] = HIGH; simIsr(pin);  g_millis += 1;
    g_pinRead[pin] = LOW;  simIsr(pin);  g_millis += 1;
  }
  g_pinRead[pin] = HIGH; simIsr(pin);
}

static void scBNC1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_isr.count(PIN_RESETBTN) == 1, "a reset gomb megszakitasa el");

  btnResetLatched = false; btnResetDownAt = 0;
  gombNyomasPattogva(PIN_RESETBTN, 300);
  CHECK(btnResetLatched, "a pattogo, 300 ms-os nyomast is elfogadja");
  CHECK(btnResetDownAt == 0,
        "es a felengedesi pattogas utan NEM marad folyamatban levo lenyomas");

  btnResetLatched = false;
  g_millis += 10u * 1000;
  g_pinRead[PIN_RESETBTN] = HIGH; simIsr(PIN_RESETBTN);
  CHECK(!btnResetLatched,
        "kesobbi maganyos felfuto el NEM reteszel (nincs elavult lenyomas-ido)");
}

static void scBNC2() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  btnResetLatched = false; btnResetDownAt = 0;
  gombNyomasPattogva(PIN_RESETBTN, 5);
  CHECK(!btnResetLatched, "a pattogo 5 ms-os tuske sem reteszel");
  bool restarted = false;
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "es nem is indit ujra");

  btnResetLatched = false; btnResetDownAt = 0;
  gombNyomasPattogva(PIN_RESETBTN, 50);
  CHECK(btnResetLatched, "50 ms tenyleges tartas viszont igen");
}

static void scBNC3() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  btnResetLatched = false; btnResetDownAt = 0;
  timing.resetBtnDownSince = 0;
  bool restarted = false;
  try {
    for (int i = 0; i < 100; i++) {
      g_pinRead[PIN_RESETBTN] = LOW;  simIsr(PIN_RESETBTN); g_millis += 10;
      resetbutton();
      g_pinRead[PIN_RESETBTN] = HIGH; simIsr(PIN_RESETBTN); g_millis += 10;
      resetbutton();
    }
  } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "a 10 ms-os recseges 2 masodpercig sem indit ujra");
  CHECK(!btnResetLatched, "es nem is reteszel");
}

static void scBNC4() {
  static uint32_t elengedAt;
  struct H {
    static void hook() {
      if (g_millis < elengedAt) return;

      const uint32_t ota = g_millis - elengedAt;
      g_pinRead[PIN_RESETBTN] = (ota < 40 && ((ota / 10) % 2 == 1)) ? LOW : HIGH;
    }
  };
  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  g_pinRead[PIN_RESETBTN] = LOW;
  elengedAt = g_millis + 800;
  g_onDelay = H::hook;
  bool slept = false;
  try { setup(); } catch (DeepSleepSignal&) { slept = true; }
  catch (RestartSignal&) {}
  g_onDelay = nullptr;
  CHECK(!slept, "a pattogva elengedett ebreszto gombnyomas NEM beragadas");
  CHECK(serialHas("Elengedtek"), "es fel is ismeri, hogy elengedtek");
}

static void scSER6() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_serialBaud == 115200, "115200 baud");
  CHECK(g_serialOn, "a setup() vegen nyitva van");

  CHECK(g_serialFirstWriteMs >= 500,
        "az elso kiirt sor csak a CDC beallasa utan megy ki");

  egyResetUtanAlvas();
  CHECK(!g_serialOn, "alvas elott a Serial.end() lefutott");
  CHECK(logIndex("Serial.flush") < logIndex("Serial.end"),
        "es a flush MEGELOZTE az end-et");
  CHECK(logIndex("Serial.end") < logIndex("DEEP_SLEEP"),
        "a lezaras az elalvas ELOTT tortent");
  CHECK(g_serialWritesAfterEnd == 0,
        "a lezaras utan egyetlen sort sem irtunk (nem veszne el semmi)");
}

static void scSER7() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_gpioHoldFails = true;
  g_pinRead[PIN_RESETBTN] = LOW;
  bool slept = false;
  try { setup(); } catch (DeepSleepSignal&) { slept = true; }
  CHECK(slept, "beragadt gomb -> alvas");
  CHECK(serialHas("rogzitese"), "a hold hibajarol kiirt figyelmeztetes");
  CHECK(g_serialWritesAfterEnd == 0, "a lezaras utan nem irunk");
  const int fig = serialIndex("rogzitese");
  CHECK(fig >= 0 && logIndex("Serial.flush") >= 0, "volt flush");
  CHECK(g_serialFlushedAll,
        "az UTOLSO kiirt sor is atment a flush-on (nem veszett el)");
}

static void routerOffline(bool offline) {
  wifiSim.willConnect = !offline;
  pingSim.ok         = !offline;
  g_httpBeginOk      = !offline;
}

static uint32_t pwrBackAt = 0;
static uint32_t pwrFirstRelayHigh = 0;
static void pwrHook() {
  if (pwrBackAt && g_millis >= pwrBackAt) routerOffline(false);
  if (!pwrFirstRelayHigh && g_pinState[PIN_RELAY] == HIGH) pwrFirstRelayHigh = g_millis;
}
static void pwrArm(uint32_t backAt) {
  pwrBackAt = backAt; pwrFirstRelayHigh = 0; g_onDelay = pwrHook;
}

static void scPWR1() {
  coldBoot(true, "TestNet", "pw", "", "");
  routerOffline(true);
  const uint32_t routerBootMs = 180u * 1000;

  rtcEvMagic = 0; rtcEvNext = 0; rtcWdtMagic = 0; rtcWdtResets = 3;
  g_resetReason = ESP_RST_POWERON;

  setup();
  CHECK(rtcWdtResets == 0, "aramszunet: a watchdog szamlalo tiszta lappal indul");

  const uint32_t t0 = timing.startMillis;
  pwrArm(t0 + routerBootMs);
  int guard = 0;
  try { while (uiFlags.firstStart && ++guard < 2000000) loop(); }
  catch (DeepSleepSignal&) {}
  g_onDelay = nullptr;
  const uint32_t dt = g_millis - t0;

  printf("     [info] a router %u mp-kor allt fel; az ESP %u mp-kor kezdett tesztelni\n",
         routerBootMs / 1000, dt / 1000);
  CHECK(pwrFirstRelayHigh == 0,
        "az ESP EGYSZER SEM vette el a router aramat a bootolasa alatt");
  CHECK(dt >= routerBootMs, "megvarta, amig a router tenylegesen felallt");
  CHECK(dt < routerBootMs + 90u * 1000,
        "de nem varta ki feleslegesen a teljes 10 percet sem");
  CHECK(g_pinState[PIN_RELAY] == LOW, "a rele vegig LOW - a router kapja az aramot");
}

static void scPWR2() {
  coldBoot(true, "TestNet", "pw", "", "");
  routerOffline(true);
  setup();
  const uint32_t t0 = timing.startMillis;
  pwrArm(0);
  int guard = 0;
  try { while (pwrFirstRelayHigh == 0 && ++guard < 2000000) loop(); }
  catch (DeepSleepSignal&) {}
  g_onDelay = nullptr;
  CHECK(pwrFirstRelayHigh != 0, "vegtelenul nem var - egyszer beavatkozik");
  const uint32_t elso = pwrFirstRelayHigh - t0;
  printf("     [info] a router elso ujrainditasa a bootolas utan %.1f perckor\n",
         elso / 60000.0);
  CHECK(elso >= 10u * 60 * 1000,
        "a 10 perces turelmi ido ELOTT biztosan nem nyul a routerhez");
  CHECK(elso < 20u * 60 * 1000, "de kb. negyed oran belul beavatkozik");
}

static void scPWR3() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  int guard = 0;
  while (uiFlags.firstStart && ++guard < 2000000) loop();
  CHECK(!uiFlags.firstStart, "monitor uzemben vagyunk");

  const uint32_t kihuzta = g_millis;
  routerOffline(true);
  pwrArm(0);
  guard = 0;
  try { while (pwrFirstRelayHigh == 0 && ++guard < 2000000) loop(); }
  catch (DeepSleepSignal&) {}
  g_onDelay = nullptr;
  CHECK(pwrFirstRelayHigh != 0, "vegul az ESP is ujrainditja a routert");
  const uint32_t turelem = pwrFirstRelayHigh - kihuzta;
  printf("     [info] turelmi ido a kezi aramtalanitastol az ESP sajat "
         "router-resetjeig: %.1f perc\n", turelem / 60000.0);
  CHECK(turelem >= 2u * 60 * 1000,
        "de legalabb ket percig var - ennyi alatt a router vissza is dughato");
  CHECK(serialHas("WiFi disconnected before test!"),
        "es a soros porton meg is mondja, hogy elveszett a halozat");
}

static void scPWR4() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test";
  setup();
  int guard = 0;
  while (uiFlags.firstStart && ++guard < 2000000) loop();

  const uint32_t kihuzta = g_millis;
  routerOffline(true);
  pwrArm(kihuzta + 90u * 1000);
  guard = 0;
  try {
    while (++guard < 2000000 && g_millis - kihuzta < 8u * 60 * 1000) loop();
  } catch (DeepSleepSignal&) {}
  g_onDelay = nullptr;
  printf("     [info] elso rele-impulzus a kezi ki/be dugas utan: %s\n",
         pwrFirstRelayHigh ? "VOLT" : "nem volt");
  CHECK(pwrFirstRelayHigh == 0,
        "idoben visszadugott routernel az ESP NEM nyul a relehez");
  CHECK(serialHas("WIFI RECONNECTED!"), "hanem szepen visszacsatlakozik");
}

static void scSH1() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk, a portal fut");

  endConfigWrite();
  lockConfigBeforeShutdown();
  CHECK(configWriteInProgress(), "a leallasi ut MEG IS SZEREZTE a zarat, nem csak megvarta");
  CHECK(!beginConfigWrite(), "igy egy beerkezo mentes mar nem tud irast inditani");

  CHECK(postConfig("Halozat", "jelszo123", "", "") == 503,
        "az utolso pillanatban erkezo mentes 503-at kap, nem tepi szet a fajlt");
  CHECK(g_fs.find("/ssid.txt") == g_fs.end(), "es tenyleg semmit nem irt ki");

  (void)beginConfigWrite();
  const uint32_t t0 = g_millis;
  lockConfigBeforeShutdown();
  const uint32_t dt = g_millis - t0;
  CHECK(dt >= 5000 && dt < 5300, "beragadt jelzonel 5 mp utan tovabblep, nem fagy le");
  CHECK(serialHas("5 mp alatt sem fejezodott be"), "es ezt ki is mondja a soros porton");
}

static void scSH2() {
  struct Eset { const char* nev; uint64_t varhatoUs; bool gombEbresztes; };

  {
    coldBoot(true, "TestNet", "pw", "", "");
    g_pinRead[PIN_RESETBTN] = LOW;
    uint64_t us = 0; bool slept = false;
    try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
    CHECK(slept && us == 60ULL*1000000ULL, "beragadt gomb: 60 mp");
    CHECK(g_gpioWakeMask == 0, "beragadt gomb: gombebresztes NINCS (boot loop ellen)");
  }

  {
    coldBoot(false, "", "", "", "");
    setup();
    CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk");
    bool slept = false; uint64_t us = 1;
    try {
      for (int i = 0; i < 4000000 && !slept; i++) loop();
    } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
    CHECK(slept, "az AP hatarido lejartaval elalszik");
    CHECK(us == 0, "AP idotullepes: idozitett ebresztes NINCS");
    CHECK(g_gpioWakeMask == (1ULL << PIN_RESETBTN), "de a reset gomb felebreszti");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    g_fsMountOk = false;
    setup();
    CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL lett");
    bool slept = false; uint64_t us = 1;
    try {
      for (int i = 0; i < 4000000 && !slept; i++) loop();
    } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
    CHECK(slept, "5 perc hibajelzes utan elalszik");
    CHECK(us == 0, "vegzetes hiba: idozitett ebresztes NINCS");
    CHECK(g_gpioWakeMask == (1ULL << PIN_RESETBTN), "de a reset gomb felebreszti");
  }

  {
    coldBoot(true, "TestNet", "pw", "", "");
    setup();
    const uint64_t us = egyResetUtanAlvas();
    CHECK(us == 3600ULL*1000000ULL, "router reset utan sincs halozat: 1 ora");
    CHECK(g_gpioWakeMask == (1ULL << PIN_RESETBTN), "es a reset gomb is felebreszti");
  }
}

static uint32_t sh3ReleaseAt = 0;
static void sh3Release() {
  if (g_millis >= sh3ReleaseAt) g_pinRead[PIN_RESETBTN] = HIGH;
}

static void scSH3() {
  const uint32_t probak[] = { 100, 300, 500, 700, 1000, 2000, 4000 };
  uint32_t leghosszabbOk = 0, legrovidebbAlvas = 0xFFFFFFFFu;
  for (uint32_t hossz : probak) {
    coldBoot(true, "TestNet", "pw", "", "", 500, true);
    g_pinRead[PIN_RESETBTN] = LOW;
    sh3ReleaseAt = g_millis + hossz;
    g_onDelay = sh3Release;
    bool slept = false;
    try { setup(); } catch (DeepSleepSignal&) { slept = true; }
    catch (RestartSignal&) {}
    g_onDelay = nullptr;
    if (slept) { if (hossz < legrovidebbAlvas) legrovidebbAlvas = hossz; }
    else       { if (hossz > leghosszabbOk)    leghosszabbOk = hossz; }
  }
  printf("     [info] ebresztesnek szamit: <= %u ms nyomas; beragadasnak: >= %u ms\n",
         leghosszabbOk, legrovidebbAlvas);
  CHECK(leghosszabbOk >= 2000,
        "meg egy 2 masodperces ebreszto gombnyomas sem szamit beragadasnak");
  CHECK(legrovidebbAlvas > 2000,
        "a hatar az emberi gombnyomas hossza FOLOTT van");

  coldBoot(true, "TestNet", "pw", "", "", 500, true);
  g_pinRead[PIN_RESETBTN] = LOW;
  bool slept = false; uint64_t us = 0;
  try { setup(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
  CHECK(slept, "a vegig nyomva tartott gomb viszont beragadasnak szamit");
  CHECK(us == 60ULL*1000000ULL, "es 60 mp mulva ujraprobal, nem 1 ora mulva");
  CHECK(g_gpioWakeMask == 0, "gombebresztes nelkul - kulonben azonnal ujraebredne");
}

static void scLOG6() {
  coldBoot(false, "", "", "", "");
  setup();

  rtcEvMagic = 0; rtcEvNext = 0;
  logEvent((EventCode)2, 0);
  rtcEvNext = 0xFFFFFFF0u;

  for (uint16_t i = 0; i < 40; i++) logEvent((EventCode)2, i);
  CHECK(rtcEvNext == 24u, "az rtcEvNext korbefordult (0xFFFFFFF0 + 40 = 24)");

  bool megvan[40] = { false };
  for (int i = 0; i < 32; i++) {
    const uint16_t v = rtcEvents[i].param;
    if (v < 40) megvan[v] = true;
  }
  int hianyzik = 0;
  for (uint16_t v = 8; v < 40; v++) if (!megvan[v]) hianyzik++;
  CHECK(hianyzik == 0, "a fordulas utan is mind a 32 legutobbi ertek megvan");
  CHECK(!megvan[7], "a kiszorult regi tenyleg felulirodott");

  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  size_t sorok = 0, tol = 0;
  while ((tol = b.find("<tr><td>", tol)) != std::string::npos) { sorok++; tol += 8; }
  CHECK(req._code == 200, "a /log a fordulas utan is 200-at ad");
  size_t rtcSor6 = 0, rp6 = 0;
  while ((rp6 = b.find("<td>RTC</td>", rp6)) != std::string::npos) { rtcSor6++; rp6++; }
  CHECK(rtcSor6 == 24, "a fordulas utan az RTC reszbol atmenetileg csak evTotal sor");
  CHECK(b.find("</table>") != std::string::npos, "a tabla rendesen lezarul");

  rtcEvNext = 0;
  CHECK(!lastEventWas(2), "rtcEvNext == 0 eseten nem indexel ki, false-t ad");

  CHECK(!stuckCycleAlreadyLogged(0), "a beragadt-gomb olvaso sem indexel ki");

  rtcEvMagic = 0; rtcEvNext = 0;
  g_fs.erase("/evlog.bin");
  logEvent((EventCode)2, 0);
  rtcEvents[0].uptimeSec = 0xFFFFFFFFu;
  AsyncWebServerRequest max; g_handlers["/log#1"](&max);
  CHECK(max._body.find("1193046:28:15") != std::string::npos,
        "a legnagyobb uptime is helyesen, csonkitas nelkul jelenik meg");
}

static void scAP1() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "192.168.1.200", "192.168.1.1") == 200,
        "elso mentes statikus IP-vel");
  CHECK(g_fs["/ip.txt"] == "192.168.1.200", "az IP elmentve");
  CHECK(g_fs["/gateway.txt"] == "192.168.1.1", "a gateway elmentve");
  clearRestartRequest();

  CHECK(postConfig("Halozat", "ujJelszo456", "", "") == 200, "masodik mentes");
  printf("     [info] a masodik mentes utan /ip.txt='%s' /gateway.txt='%s'\n",
         g_fs["/ip.txt"].c_str(), g_fs["/gateway.txt"].c_str());
  CHECK(g_fs["/ip.txt"].empty() && g_fs["/gateway.txt"].empty(),
        "ures cimmezok -> torles, azaz DHCP (ez a SZANDEKOLT ut vissza DHCP-re)");
}

static void scFS11() {
  char out[40];
  coldBoot(true, "x", "", "", "");
  g_fs["/t.txt"] = "MyNetwork\r\n";
  CHECK(readConfigValue(LittleFS, "/t.txt", out, sizeof(out)) == (ConfigStatus)0
        && strcmp(out, "MyNetwork") == 0, "CRLF: a \\r is levagodik");
  g_fs["/t.txt"] = "  Halozat  \n masodik sor";
  CHECK(readConfigValue(LittleFS, "/t.txt", out, sizeof(out)) == (ConfigStatus)0
        && strcmp(out, "Halozat") == 0, "csak az elso sor, trimmelve");
  g_fs["/t.txt"] = "   \t  \r\n";
  CHECK(readConfigValue(LittleFS, "/t.txt", out, sizeof(out)) == (ConfigStatus)0
        && out[0] == '\0', "csupa whitespace -> ures ertek (nem hiba)");
  g_fs["/t.txt"] = "";
  CHECK(readConfigValue(LittleFS, "/t.txt", out, sizeof(out)) == (ConfigStatus)0
        && out[0] == '\0', "ures fajl -> ures ertek (a wifireset ezt hagyja)");
}

static void scFS12() {
  char out[16];
  coldBoot(true, "x", "", "", "");
  std::string szemet("AB", 2); szemet.push_back('\0'); szemet += "CDEFGH";
  g_fs["/t.txt"] = szemet;
  CHECK(readConfigValue(LittleFS, "/t.txt", out, sizeof(out)) == (ConfigStatus)0,
        "beagyazott NUL: nem hiba, csak rovid ertek");
  CHECK(strcmp(out, "AB") == 0, "a NUL-ig tarto resz jon vissza");

  g_fs["/t.txt"] = std::string(200, 'x');
  CHECK(readConfigValue(LittleFS, "/t.txt", out, sizeof(out)) == (ConfigStatus)0,
        "tul hosszu fajl: csonkolas, nem hiba");
  CHECK(strlen(out) == sizeof(out) - 1, "pontosan a puffer hataraig");
}

static void scFS13() {
  coldBoot(false, "", "", "", "");
  setup();

  g_fsCapacity = 12;
  const int kod = postConfig("Halozat", "hosszabbJelszo", "", "");
  CHECK(kod == 500, "irasi hibanal 500-as valasz");
  CHECK(!configWriteInProgress(), "a ZAR felszabadult (a mentes nem ragadt be)");
  CHECK(!restartRequested(), "es NINCS ujrainditas utemezve");

  g_pinRead[3] = LOW;
  bool restarted = false;
  try { resetbutton(); g_millis += 100; resetbutton(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a reset gomb az irasi hiba utan is mukodik");
}

static void scFS14() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk, a portal fut");

  CHECK(postConfig("RegiHalozat", "regiJelszo", "", "") == 200, "elso mentes OK");
  const std::string regiPass = g_fs["/pass.txt"];
  CHECK(!regiPass.empty(), "a regi jelszo a fajlban van");
  clearRestartRequest();

  g_fsCapacity = g_fs["/ssid.txt"].size() + 4;
  const int kod = postConfig("UjHalozat", "ujHosszabbJelszo", "", "");
  CHECK(kod == 500, "a mentes hibat jelez");

  printf("     [info] a mentes utan /ssid.txt='%s', /pass.txt hossza=%u\n",
         g_fs["/ssid.txt"].c_str(), (unsigned)g_fs["/pass.txt"].size());
  CHECK(g_fs["/pass.txt"] != regiPass,
        "a REGI jelszo mar NINCS meg (a FILE_WRITE megnyitaskor csonkol)");

  g_fsCapacity = 0;
  CHECK(postConfig("UjHalozat", "ujHosszabbJelszo", "", "") == 200,
        "a hely felszabadulasa utan a mentes ujra sikerul");
  CHECK(!g_fs["/pass.txt"].empty(), "es a jelszo helyreall");
}

static void scLAT1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_isr.count(3) == 1 && g_isr.count(2) == 1,
        "mindket gomb megszakitasa elesitve");
  gombNyomas(3, 200);
  CHECK(btnResetLatched, "az ISR reteszelte a teljes erteku nyomast");
  CHECK(g_pinRead[3] == HIGH, "a gomb kozben fel is lett engedve");
  bool restarted = false;
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a kovetkezo pollozas mar ujrainditja az eszkozt");
}

static void scLAT2() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  gombNyomas(3, 5);
  CHECK(!btnResetLatched, "5 ms-os tuske NEM reteszel");
  bool restarted = false;
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "es nem is indit ujra");
  gombNyomas(3, 49);
  CHECK(!btnResetLatched, "49 ms sem eleg (BUTTON_DEBOUNCE_MS = 50)");
  gombNyomas(3, 50);
  CHECK(btnResetLatched, "50 ms viszont igen");
}

static void scLAT3() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_fs["/ssid.txt"] = "TestNet";
  gombNyomas(2, 200);
  CHECK(btnWifiResetLatched, "a wifireset is reteszelt");
  bool restarted = false;
  try { wifiresetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "ujraindult");
  CHECK(g_fs["/ssid.txt"].empty(), "es a mentett SSID torlodott");
}

static void scLAT4() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  gombNyomas(3, 200);
  CHECK(btnResetLatched, "reteszelve");
  (void)beginConfigWrite();
  bool restarted = false;
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "mentes alatt NEM indit ujra");
  CHECK(btnResetLatched, "a jelzo MEGMARADT - a nyomas nem veszett el");
  endConfigWrite();
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a mentes utan viszont lefut");
}

static void scLAT5() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_pinRead[3] = LOW; simIsr(3);
  g_millis += 10u * 60 * 1000;
  CHECK(!btnResetLatched, "beragadt gomb NEM reteszel (nincs felfuto el)");
  CHECK(btnResetDownAt != 0, "de a lenyomas ideje rogzult");

  bool restarted = false;
  try { resetbutton(); g_millis += 100; resetbutton(); }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a pollozott ag a tartos nyomast tovabbra is elfogadja");
}

static void scLAT6() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  gombNyomas(3, 200);
  CHECK(btnResetLatched, "reteszelve");

  (void)beginConfigWrite();
  bool restarted = false;
  try { restartFromButton("teszt"); } catch (RestartSignal&) { restarted = true; }
  CHECK(!restarted, "foglalt zarnal nem indul ujra");
  CHECK(btnResetLatched, "es a RETESZ MEGMARAD - a nyomas nem veszett el");

  endConfigWrite();
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a felszabadulas utan a reteszelt nyomas lefut");
}

static void scLAT7() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_isr.count(3) == 1, "eleles utan van kezelo a reset gombon");
  try { enterDeepSleep(0); } catch (DeepSleepSignal&) {}
  CHECK(g_isr.count(3) == 0 && g_isr.count(2) == 0,
        "alvas elott mindket megszakitas levalasztva");
  const int det = logIndex("detachInterrupt(3)");
  const int arm = logIndex("gpio_wakeup(");
  CHECK(det >= 0, "a levalasztas tenyleg megtortent");
  CHECK(arm < 0 || det < arm,
        "MEG az ebresztoforras elesitese ELOTT");
}

static void scBTN4() {
  coldBoot(false, "", "", "", "");
  setup();
  endConfigWrite();
  g_pinRead[3] = LOW;
  resetbutton();
  g_millis += 100;
  bool restarted = false;
  try { resetbutton(); } catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "az ujrainditas megtortent");
  CHECK(configWriteInProgress(),
        "es kozben a zar a miénk volt - mentes nem indulhatott bele");

  coldBoot(false, "", "", "", "");
  setup();
  g_pinRead[3] = LOW;
  resetbutton();
  g_millis += 100;
  (void)beginConfigWrite();
  bool restarted2 = false;
  try { resetbutton(); } catch (RestartSignal&) { restarted2 = true; }
  CHECK(!restarted2, "folyamatban levo mentes alatt NEM indit ujra");
}

static void scSE11() {
  char enc[200];
  CHECK(encodeSecret("titkosJelszo", enc, sizeof(enc)), "a kodolas sikerult");
  coldBoot(true, "MyNetwork", enc, "", "");
  wifiSim.availableFrom = 0xFFFFFFFFu;
  setup();
  wifiSim.lastPass.clear();
  const uint32_t t0 = g_millis;
  int guard = 0;
  try { while (g_millis - t0 < 3u*60*1000 && ++guard < 200000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(!wifiSim.lastPass.empty(), "a proba tenyleg hivott WiFi.begin()-t");
  CHECK(wifiSim.lastPass == "titkosJelszo",
        "es a NYILT jelszot adta at, nem a 'v1:' kodolt alakot");
  CHECK(wifiSim.lastPass.rfind("v1:", 0) != 0, "a radioig nem jut el a kodolt alak");
}

static void scWF10() {
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "192.168.1.1");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  CHECK(staticConfigActive, "statikus IP-vel indultunk");
  CHECK(wifiSim.staticApplied, "a netif megkapta a konfigot");

  int guard = 0;
  try { while (++guard < 400000 && logIndex(RELAY_HIGH) < 0) loop(); }
  catch (DeepSleepSignal&) {}

  g_httpCode = 200; g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  guard = 0;
  try { while (++guard < 400000 && !serialHas("RESET_DELAY korai vege")) loop(); }
  catch (DeepSleepSignal&) {}

  CHECK(serialHas("RESET_DELAY korai vege"), "a korai agon jottunk ki");
  CHECK(wifiSim.staticApplied,
        "a statikus konfig ERINTETLEN maradt (nem volt disconnect(true))");
  CHECK(wifiSim.cfgIp.str() == "192.168.1.200", "es tenyleg a mentett cim el");
  CHECK(staticConfigActive, "a staticConfigActive jelzo is helyes maradt");
}

static void scCFG2() {
  coldBoot(true, "TestNet", "v1:abc", "", "");
  wifiSim.willConnect = false;
  wifiSim.authFail = true;
  try { setup(); } catch (DeepSleepSignal&) {}
  CHECK(deviceMode != (DeviceMode)2, "NEM megy vegzetes hibaba a serult jelszo miatt");
  CHECK(strcmp(pass, "v1:abc") == 0,
        "a dekodolas valtozatlanul hagyta (nem 'javitott' bele)");
  int guard = 0;
  try { while (deviceMode == (DeviceMode)0 && ++guard < 400000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(deviceMode == (DeviceMode)1, "hanem AP modba jut, ahol ujra beallithato");
}

static void scWDT9() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk");
  rtcWdtResets = 2;
  const uint32_t t0 = g_millis;
  int guard = 0;
  try {
    while (g_millis - t0 < 70u * 60 * 1000 && ++guard < 900000) {
      touchApDeadline();
      loop();
    }
  } catch (DeepSleepSignal&) {}
  printf("     [info] AP modban %u perc ebren utan a szamlalo: %u\n",
         (g_millis - t0) / 60000, (unsigned)rtcWdtResets);
  CHECK(g_millis - t0 >= 60u * 60 * 1000, "tenyleg tobb mint egy orat futottunk");
  CHECK(rtcWdtResets == 0,
        "1 ora hibatlan mukodes AP modban is nullazza a szamlalot");
}

static void scWDT8b() {
  g_wdtGapWaiver = "WDT8b: szandekosan korosan lassu flash formazas (51,5 mp)";

  const uint32_t esetek[] = { 7000, 51000 };
  for (uint32_t formazas : esetek) {
    coldBoot(true, "TestNet", "pw", "", "");
    g_fsMountMs = formazas;
    g_wdtTrack = true; g_wdtMaxFeedGap = 0; g_wdtLastFeed = g_millis;
    try { setup(); } catch (DeepSleepSignal&) {}
    g_wdtTrack = false;
    printf("     [info] %2u mp-es formazas -> leghosszabb etetes nelkuli szakasz: %.1f mp\n",
           formazas / 1000, g_wdtMaxFeedGap / 1000.0);
    CHECK(g_wdtMaxFeedGap >= formazas,
          "a formazas ideje tenyleg beleszamit (a meres ervenyes)");
    CHECK(g_wdtMaxFeedGap < 90u * 1000,
          "es meg a rossz esetben is a 90 mp-es timeout alatt marad");
  }
}

static void scLED4() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  setup();
  int guard = 0;

  try { while (++guard < 100000 && currentState != (State)1) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(currentState == (State)1, "a ket ellenorzes kozotti varakozasban vagyunk");
  CHECK(g_pinState[PIN_WIFILED] == HIGH, "es a Wi-Fi LED vilagit");

  wifiSim.willConnect = false; wifiSim.begun = false;
  const uint32_t t0 = g_millis;
  s_wifiLedOffAt = 0;
  g_onDelay = figyelWifiLed;
  guard = 0;
  try { while (++guard < 100000 && s_wifiLedOffAt == 0) loop(); }
  catch (DeepSleepSignal&) {}
  g_onDelay = nullptr;
  const uint32_t hazugsag = s_wifiLedOffAt ? s_wifiLedOffAt - t0 : 0;
  printf("     [info] a Wi-Fi LED %u mp-ig mutatott meg kapcsolatot\n", hazugsag / 1000);
  CHECK(s_wifiLedOffAt != 0, "vegul elalszik");
  CHECK(hazugsag <= 62u * 1000,
        "legkesobb a kovetkezo ellenorzeskor (percenkent)");
  CHECK(hazugsag >= 55u * 1000,
        "de a varakozas alatt tenyleg nem ellenorizzuk - ez a mert ablak");
}

static void scLED5() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  setup();
  CHECK(g_pinState[PIN_LED] == HIGH, "indulas utan vilagit");
  int guard = 0, sotet = 0;
  try {
    while (++guard < 20000) {
      loop();
      if (g_pinState[PIN_LED] != HIGH) sotet++;
    }
  } catch (DeepSleepSignal&) {}
  CHECK(sotet == 0, "normal monitor uzemben egyetlen korben sem alszik el");
}

static void scLED6() {
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(postConfig("Halozat", "jelszo123", "", "") == 200, "mentes OK");
  CHECK(restartRequested(), "ujrainditas beutemezve");
  int valtas = 0, last = g_pinState[PIN_WIFILED];
  const uint32_t t0 = g_millis;
  try {
    while (g_millis - t0 < 1500) {
      loop();
      if (g_pinState[PIN_WIFILED] != last) { valtas++; last = g_pinState[PIN_WIFILED]; }
    }
  } catch (RestartSignal&) {} catch (DeepSleepSignal&) {}
  CHECK(valtas >= 2, "a Wi-Fi LED a turelmi ido alatt is villog");
  CHECK(g_pinState[PIN_LED] == HIGH, "a statusz LED kozben vegig vilagit");
}

static void scBTN1() {
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
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1;
  g_httpFailMs = 33000;
  pingSim.ok = false;
  setup();
  const uint32_t gap = merjGombHezag([]() {
    int guard = 0;
    try {
      while (++guard < 200000 && !serialHas("Nincs internet")) loop();

      loop();
    } catch (DeepSleepSignal&) {}
  });
  printf("     [info] a blokkolo HTTP keres alatti vak ablak: %u ms\n", gap);
  CHECK(gap >= 30000, "a http.GET() alatt tenyleg nem nezzuk a gombokat");
  CHECK(gap <= 33000 + 2 * 1000 + 100,
        "de csak egy keres es a ket szomszedos ping-proba erejeig");
}

static void scBTN3() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; g_httpFailMs = 33000; pingSim.ok = true;
  setup();
  g_pinRead[3] = LOW;
  bool restarted = false;
  int guard = 0;
  try { while (++guard < 200000 && !restarted) loop(); }
  catch (RestartSignal&) { restarted = true; }
  catch (DeepSleepSignal&) {}
  CHECK(restarted, "a vegig nyomva tartott reset gomb a vak ablak utan is hat");
}

static void scOP7() {
  coldBoot(true, "TestNet", "pw", "", "");
  pingSim.ok = false;
  setup();
  const uint32_t t0 = timing.startMillis;
  const uint32_t netVissza = 210u * 1000;
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
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const uint32_t base = 0xFFFFFF00u;
  uint32_t last = base;
  uint32_t firedAt = 0;
  for (uint32_t d = 1000; d <= 180000 && firedAt == 0; d += 1000) {
    const uint32_t now = base + d;
    const uint32_t before = last;
    onlineProbeDue(last, now);
    if (last != before) firedAt = d;
  }
  CHECK(firedAt > 0, "a proba a korbefordulas utan is lefut, nem all le orokre");
  CHECK(firedAt >= 55000 && firedAt <= 65000,
        "es pontosan a ~60 mp-es utem szerint (nem azonnal, nem kesobb)");
}

static void scOP5() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  int guard = 0;
  try { while (++guard < 400000 && logIndex(RELAY_LOW + 0) >= 0
               && logIndex(RELAY_HIGH) < 0) loop(); } catch (DeepSleepSignal&) {}

  g_httpCode = 200; g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  const uint32_t t0 = g_millis;
  guard = 0;
  try { while (++guard < 400000 && !serialHas("RESET_DELAY korai vege")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("RESET_DELAY korai vege"), "a RESET_DELAY korabban veget ert");
  CHECK(g_millis - t0 < 8u * 60 * 1000, "nem a teljes 10 percet varta ki");
  CHECK(!serialHas("RESET_DELAY vege - ujracsatlakozas."),
        "a korai agon NINCS disconnect+reconnect - a kapcsolat mar igazolt");
}

static void scLEDap() {
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

  const int varhato = (int)(pulzus / 250);
  CHECK(ledValtas >= varhato - 5 && ledValtas <= varhato + 5,
        "a statusz LED 2 Hz-cel villog vegig a pulzus alatt");
  CHECK(wifiMagas == 0, "a Wi-Fi LED egyszer sem gyulladt ki - nincs is halozat");
  CHECK(g_pinState[PIN_LED] == HIGH, "a pulzus utan a statusz LED folyamatosra all vissza");
  CHECK(g_pinState[PIN_RELAY] == LOW, "es a router visszakapta az aramot");
}

static void scWDT_EARLY() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  const int wdt = serialIndex("Watchdog enabled");
  const int fs  = serialIndex("Init LittleFS.");
  CHECK(wdt >= 0, "a watchdog elesedett");
  CHECK(fs >= 0 && wdt < fs, "MEG a LittleFS csatolasa elott");
  CHECK(watchdogEnabled, "es tenyleg fel is iratkozott a loop task");
}

static void scSERidx() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  int guard = 0;
  try { while (++guard < 400000 && !serialHas("Nincs internet 1 perce")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Nincs internet 0 perce, a router sem valaszol."), "az elso korben 0 perc");
  CHECK(serialHas("Router reset legkorabban 2 perc mulva."), "es a hatralevo ido 2 perc");
  CHECK(serialHas("Nincs internet 1 perce"), "a kovetkezo korben 1 perc");
  CHECK(serialIndex("Internet ellenorzes.") < serialIndex("Nincs internet 0 perce"),
        "az ellenorzes kezdete a kiertekeles elott all");
}

static void scSERquiet() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = 200; g_httpBody = "Microsoft Connect Test"; pingSim.ok = true;
  setup();
  int guard = 0;
  try { while (++guard < 100000 && !serialHas("Successful Test")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Successful Test"), "a siker kiirodik");
  CHECK(!serialHas("Igaz ertek!"), "de az 'Igaz ertek!' mar nem");
  CHECK(!serialHas("Microsoft Connect Test"),
        "es a kapott torzset sem irjuk ki sikernel");

  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = 200; g_httpBody = "captive portal login"; pingSim.ok = false;
  setup();
  guard = 0;
  try { while (++guard < 100000 && !serialHas("Hamis ertek!")) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(serialHas("Hamis ertek!"), "elteresnel megmarad a jelzes");
  CHECK(serialHas("captive portal login"),
        "es a KAPOTT torzs is - abbol derul ki, mi ult a keresre");
}

static void scEVT1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -1; pingSim.ok = false;
  setup();
  int guard = 0;
  try { while (++guard < 400000 && !serialHas("Nincs internet")) loop(); }
  catch (DeepSleepSignal&) {}
  bool megvan = false;
  for (uint32_t i = 0; i < rtcEvNext && i < 32; i++) {
    const EventEntry& e = rtcEvents[i % 32];
    if (e.code == EV_TEST_FAIL_C) { megvan = (e.param == 1); }
  }
  CHECK(megvan, "az elso bukas EV_TEST_FAIL parametere 1, nem 0");
}

static void scSYNC1() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_millis = 100000;

  g_criticalEnters = 0;
  g_criticalMaxDepth = 0;
  requestRestart(2000);
  CHECK(g_criticalEnters == 1,
        "a requestRestart() a part EGYETLEN kritikus szakaszban irja");
  CHECK(g_criticalMaxDepth == 1, "es a szakasz nem agyazodik egymasba");
  CHECK(restartRequested(), "a keres bejegyzodott");

  CHECK(!restartRequestDue(101999), "1 ms-mal a turelmi ido elott meg nem esedekes");
  CHECK(restartRequestDue(102000), "pontosan a turelmi ido vegen esedekes");

  g_criticalEnters = 0;
  const bool due = restartRequestDue(g_millis);
  CHECK(g_criticalEnters == 1,
        "a restartRequestDue() a part EGYETLEN kritikus szakaszban olvassa");
  CHECK(!due, "a turelmi ido meg nem telt le");
}

static void scSYNC2() {
  coldBoot(true, "TestNet", "pw", "", "");
  g_millis = 100000;
  requestRestart(2000);

  CHECK(!restartRequestDue(101999), "1 ms-mal a hatarido elott meg nem esedekes");
  CHECK(restartRequestDue(102000), "a hataridon esedekes");
  CHECK(restartRequestDue(103000), "es azon tul is az marad");

  clearRestartRequest();
  CHECK(!restartRequested(), "a keres torolve");
  CHECK(!restartRequestDue(102000), "torolt keres mellett SOHA nem esedekes");
}

static void scSYNC3() {
  coldBoot(true, "TestNet", "pw", "", "");
  CHECK(!configWriteInProgress(), "indulaskor szabad a zar");
  CHECK(beginConfigWrite(), "a zar megszerezheto");
  CHECK(configWriteInProgress(), "es innentol foglaltnak latszik");
  CHECK(!beginConfigWrite(), "masodszor mar NEM szerezheto meg");
  g_criticalEnters = 0;
  g_criticalMaxDepth = 0;
  endConfigWrite();
  CHECK(g_criticalEnters == 1,
        "a feloldas is a zaron keresztul megy (nem nyers ertekadas)");
  CHECK(g_criticalMaxDepth == 1, "es nem agyazodik egymasba");
  CHECK(!configWriteInProgress(), "feloldas utan ujra szabad");
  CHECK(beginConfigWrite(), "es ujra megszerezheto");
}

static void scAP8() {
  coldBoot(false, "", "", "", "");
  setup();

  for (int i = 0; i < 32; i++) ssid[i] = '"';
  ssid[32] = '\0';
  for (int i = 0; i < 15; i++) { ipStr[i] = '"'; gatewayStr[i] = '"'; }
  ipStr[15] = '\0'; gatewayStr[15] = '\0';

  AsyncWebServerRequest req; g_handlers["/#1"](&req);
  const size_t meret = req._body.size();
  printf("     [info] az urlap legrosszabb esetben %u bajt (a puffer 1536)\n",
         (unsigned)meret);
  CHECK(req._code == 200, "az urlap kiszolgalodik");
  CHECK(meret < 1536, "es belefer a stream 1536 bajtos kezdo pufferebe");

  strcpy(ssid, "MyHomeNetwork");
  strcpy(ipStr, "192.168.1.200");
  strlcpy(gatewayStr, "192.168.1.1", IPSTR_MAX_LEN_C + 1);
  AsyncWebServerRequest req2; g_handlers["/#1"](&req2);
  printf("     [info] tipikus esetben %u bajt\n", (unsigned)req2._body.size());
  CHECK(req2._body.size() < meret, "a tipikus eset kisebb a legrosszabbnal");
}

static void scCOV1() {
  coldBoot(false, "", "", "", "");
  CHECK(strcmp(eventName(0), "?") == 0, "a 0-as kod ismeretlen -> \"?\"");
  CHECK(strcmp(eventName(200), "?") == 0, "egy jovobeli kod is \"?\"");
  CHECK(strcmp(eventName(1), "BOOT") == 0, "az ismert kod tovabbra is helyes");
}

static void scCOV2() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);

  g_fs["/evlog.bin"] = std::string(sizeof(EvFileHeader) + 4096 * sizeof(EventEntry), '\xA5');

  const uint8_t ORSZEM = 0x5A;
  EventEntry puffer[EVFILE_SIZE_C + 8];
  memset(puffer, 0, sizeof(puffer));
  memset(&puffer[EVFILE_SIZE_C], ORSZEM, 8 * sizeof(EventEntry));

  EvFileHeader fej;
  memset(&fej, 0, sizeof(fej));
  auto orszemEp = [&]() {
    const uint8_t* p = (const uint8_t*)&puffer[EVFILE_SIZE_C];
    for (size_t i = 0; i < 8 * sizeof(EventEntry); i++) if (p[i] != ORSZEM) return false;
    return true;
  };

  fej.count = 0;
  CHECK(!loadEventLogRange(fej, 0, fej.count, puffer), "count = 0 -> elutasitva");
  fej.count = 0xFFFF;
  CHECK(!loadEventLogRange(fej, 0, fej.count, puffer), "count = 65535 -> elutasitva");
  CHECK(orszemEp(), "es NEM irt tul: a puffer mogotti orszemek epek (65535)");
  fej.count = EVFILE_SIZE_C + 1;
  CHECK(!loadEventLogRange(fej, 0, fej.count, puffer), "count = fajlgyuru+1 -> elutasitva");
  CHECK(orszemEp(), "es NEM irt tul (fajlgyuru+1) - EGY elemmel sem");

  fej.count = EVFILE_SIZE_C;
  loadEventLogRange(fej, 0, fej.count, puffer);
  CHECK(orszemEp(), "a megengedett maximumnal sem lep at a hataron");
}

static void scCOV3() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);

  logEvent((EventCode)1, 1);
  logEvent((EventCode)2, 2);
  CHECK(saveEventLog("COV3"), "a naplo mentese sikerult");
  EvFileHeader fej;
  memset(&fej, 0, sizeof(fej));
  CHECK(loadEventLogHeader(fej), "a fejlec beolvashato");
  CHECK(fej.count >= 2, "a fejlec a bejegyzesek szamat mondja");

  g_fs.erase("/evlog.bin");
  EventEntry puffer[EVLOG_SIZE_C];
  memset(puffer, 0, sizeof(puffer));
  CHECK(!loadEventLogRange(fej, 0, fej.count, puffer),
        "a ket olvasas kozott eltunt fajl nem omlik ossze, csak hamisat ad");
}

static void scCOV4() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  g_fs["/ssid.txt"] = "";
  g_fsDirs.insert("/ssid.txt");
  char puffer[SSID_MAX_LEN_C + 1];
  const int st = (int)readConfigValue(LittleFS, "/ssid.txt", puffer, sizeof(puffer));
  CHECK(st == 2, "konyvtar a konfig utvonalon -> CONFIG_ERROR, nem osszeomlas");
  CHECK(serialHas("failed to open /ssid.txt"), "es a soros porton meg is mondja");
  g_fsDirs.clear();

  g_fs["/ssid.txt"] = "RendesHalo";
  CHECK((int)readConfigValue(LittleFS, "/ssid.txt", puffer, sizeof(puffer)) == 0,
        "a konyvtar eltavolitasa utan a rendes olvasas valtozatlan");
  CHECK(strcmp(puffer, "RendesHalo") == 0, "es a helyes erteket adja");
}

static void scCOV5() {
  coldBoot(false, "", "", "", "");
  setup();
  g_fs["/ssid.txt"] = "TorlendoHalo";
  CHECK(beginConfigWrite(), "a zarat mi szereztuk meg (mentes indul)");
  bool ujraindult = false;
  try { doWifiReset(); } catch (RestartSignal&) { ujraindult = true; }
  CHECK(!ujraindult, "foglalt zarnal a wifireset NEM indit ujra");
  CHECK(g_fs.count("/ssid.txt") && g_fs["/ssid.txt"] == "TorlendoHalo",
        "es NEM is torol - a mentest nem vagja fel");

  endConfigWrite();
  try { doWifiReset(); } catch (RestartSignal&) { ujraindult = true; }
  CHECK(ujraindult, "a zar felszabadulasa utan a wifireset ujraindit");
  CHECK(!g_fs.count("/ssid.txt") || g_fs["/ssid.txt"].empty(),
        "es ekkor tenyleg torolt");
}

static void scCOV6() {
  coldBoot(true, "TestNet", "pw", "192.168.1.200", "192.168.1.1");
  setup();
  staticConfigActive = false;
  CHECK(!gatewayUnreachable(), "statikus konfig nelkul nincs mit ellenorizni");
  staticConfigActive = true;
  pingSim.perTarget["192.168.1.1"] = true;
  CHECK(!gatewayUnreachable(), "valaszolo gateway -> elerheto");
  pingSim.perTarget["192.168.1.1"] = false;
  CHECK(gatewayUnreachable(), "nem valaszolo gateway -> elerhetetlen");
  wifiSim.willConnect = false; wifiSim.begun = false;
  const int elotte = pingSim.calls;
  CHECK(gatewayUnreachable(), "kapcsolat nelkul (0.0.0.0 gateway) -> elerhetetlen");
  CHECK(pingSim.calls == elotte, "es nem pingel vakon 0.0.0.0-t");
}

static int  g_ilvDepth   = 0;

static int  g_ilvHits    = 0;
static int  g_ilvEvery   = 1;
static int  g_ilvCounter = 0;
static int  g_ilvWhich   = 0;
static int  g_ilvPostSeq = 0;

static bool g_ilvExitLocked = false;
static bool g_ilvExitSeen   = false;

static void ilvNoteExit() {
  g_ilvExitSeen = true;
  g_ilvExitLocked = configWriteInProgress();
}

static void ilvHttpHook() {
  if (g_ilvDepth > 0) return;
  if (g_ilvEvery > 1 && (++g_ilvCounter % g_ilvEvery) != 0) return;
  g_ilvDepth++;
  try {
    AsyncWebServerRequest req;
    if (g_ilvWhich == 0) {
      g_handlers["/ping#1"](&req);
    } else if (g_ilvWhich == 1) {
      g_handlers["/log#1"](&req);
    } else {
      char s[40]; snprintf(s, sizeof(s), "Halo%d", ++g_ilvPostSeq);
      req.addParam("ssid", s);
      req.addParam("pass", "jelszo12345");
      g_handlers["/#2"](&req);
    }
    g_ilvHits++;
  } catch (RestartSignal&) {
  } catch (DeepSleepSignal&) {
  }
  g_ilvDepth--;
}

static int  g_ilvLoopBudget = 0;

static bool g_ilvForceExit = false;
static bool g_ilvLockedWhileLooping = false;
static void ilvLoopHook() {
  if (g_ilvDepth > 0 || g_ilvLoopBudget <= 0) return;
  g_ilvLoopBudget--;
  if (g_ilvForceExit) apDeadline = millis() - 1;
  if (configWriteInProgress()) g_ilvLockedWhileLooping = true;
  g_ilvDepth++;
  g_ilvHits++;
  try {
    loop();
  } catch (RestartSignal&) {
    ilvNoteExit();
  } catch (DeepSleepSignal&) {
    ilvNoteExit();
  }
  g_ilvDepth--;
}

static void ilvReset() {
  g_ilvDepth = 0; g_ilvHits = 0; g_ilvCounter = 0; g_ilvPostSeq = 0;
  g_ilvLoopBudget = 0; g_ilvForceExit = false; g_ilvLockedWhileLooping = false;
  g_ilvExitLocked = false; g_ilvExitSeen = false;
  g_onDelay = nullptr; g_onFsWrite = nullptr;
}

static void scILV1() {
  ilvReset();
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP modban vagyunk (van HTTP felulet)");
  g_ilvWhich = 0; g_ilvEvery = 1;
  g_onDelay = ilvHttpHook;
  bool kilepett = false;
  int guard = 0;
  try {
    while (++guard < 4000) loop();
  } catch (DeepSleepSignal&) { kilepett = true; ilvNoteExit(); }
    catch (RestartSignal&)   { kilepett = true; ilvNoteExit(); }
  g_onDelay = nullptr;
  printf("     [info] %d osszefonas, %d loop iteracio\n", g_ilvHits, guard);
  CHECK(g_ilvHits > 100, "a /ping tenylegesen befutott a loop() koze");
  CHECK(g_criticalMaxDepth <= 1, "a kritikus szakasz nem agyazodott egymasba");

  CHECK(!kilepett || g_ilvExitLocked,
        "ha megis kilepett, a mentesi zart megszerezve tette");
  CHECK(deviceMode == (DeviceMode)1, "vegig AP modban maradt (a keep-alive hatott)");
}

static void scILV2() {
  ilvReset();
  coldBoot(false, "", "", "", "");
  setup();
  g_ilvWhich = 1; g_ilvEvery = 3;
  g_onDelay = ilvHttpHook;
  int guard = 0;
  try {
    while (++guard < 3000) {
      loop();
      if (guard % 200 == 0) logEvent((EventCode)2, (uint16_t)guard);
    }
  } catch (DeepSleepSignal&) { ilvNoteExit(); }
    catch (RestartSignal&)   { ilvNoteExit(); }
  g_onDelay = nullptr;
  printf("     [info] %d /log lekeres a naplo irasa kozben\n", g_ilvHits);
  CHECK(g_ilvHits > 50, "a /log tenylegesen befutott a naplo irasa koze");
  CHECK(g_criticalMaxDepth <= 1, "a kritikus szakasz nem agyazodott egymasba");
  CHECK(rtcEvMagic == 0x42415A4DUL, "az esemenynaplo magicje ep maradt");
}

static void scILV3() {
  ilvReset();
  coldBoot(false, "", "", "", "");
  setup();
  g_ilvWhich = 2; g_ilvEvery = 5;
  g_onDelay = ilvHttpHook;
  int guard = 0;
  bool ujraindult = false;
  const int INJEKTALAS_VEGE = 600;
  try {
    while (++guard < 6000) {
      if (guard == INJEKTALAS_VEGE) {
        g_onDelay = nullptr;
        CHECK(restartRequested(), "az injektalas vegen all fuggo ujrainditas");
      }
      loop();
    }
  } catch (RestartSignal&)   { ujraindult = true; ilvNoteExit(); }
    catch (DeepSleepSignal&) { ilvNoteExit(); }
  g_onDelay = nullptr;
  printf("     [info] %d POST mentes, %d iteracio, ujraindult=%s\n",
         g_ilvHits, guard, ujraindult ? "igen" : "nem");
  CHECK(g_ilvHits > 5, "a POST mentes tenylegesen befutott a loop() koze");
  CHECK(g_criticalMaxDepth <= 1, "a kritikus szakasz nem agyazodott egymasba");

  CHECK(ujraindult, "az injektalas abbahagyasa utan az ujrainditas vegrehajtodott");
  CHECK(guard > INJEKTALAS_VEGE, "es tenyleg csak azutan, nem kozben");
  CHECK(g_ilvExitLocked,
        "az ujrainditas a mentesi zart MEGSZEREZVE tortent (nem vagta fel a mentest)");

  CHECK(g_fs.count("/ssid.txt") && g_fs["/ssid.txt"].rfind("Halo", 0) == 0,
        "a mentett SSID teljes ertek, nem csonk");
}

static void scILV4() {
  ilvReset();
  coldBoot(false, "", "", "", "");
  setup();
  CHECK(deviceMode == (DeviceMode)1, "AP mod");
  g_ilvForceExit = true;
  g_ilvLoopBudget = 3;
  g_onFsWrite = ilvLoopHook;
  AsyncWebServerRequest req;
  req.addParam("ssid", "OsszefontHalo");
  req.addParam("pass", "jelszo12345");
  bool kivetel = false;
  try {
    g_handlers["/#2"](&req);
  } catch (RestartSignal&)   { kivetel = true; ilvNoteExit(); }
    catch (DeepSleepSignal&) { kivetel = true; ilvNoteExit(); }
  g_onFsWrite = nullptr;
  printf("     [info] %d loop() iteracio a flash-iras KOZBEN, zar allt=%s, "
         "kilepes=%s%s\n", g_ilvHits, g_ilvLockedWhileLooping ? "igen" : "NEM",
         g_ilvExitSeen ? "igen" : "nem",
         kivetel ? " (a kezelo maga kivetellel zarult)" : "");

  CHECK(g_ilvHits > 0, "a loop() tenylegesen befutott a flash-iras koze");
  CHECK(g_ilvLockedWhileLooping,
        "es a beekelt loop() KOZBEN tenyleg allt a mentesi zar (nyitott ablak)");

  CHECK(!g_ilvExitSeen,
        "a lejart hatarido ELLENERE sem aludt el, amig a mentes folyt");
  CHECK(g_criticalMaxDepth <= 1, "a kritikus szakasz nem agyazodott egymasba");
  CHECK(g_fs.count("/ssid.txt") && g_fs["/ssid.txt"] == "OsszefontHalo",
        "a mentett SSID az osszefonas ellenere is teljes es helyes");
}

static void scILV5() {
  ilvReset();
  coldBoot(false, "", "", "", "");
  setup();
  g_onFsWrite = ilvLoopHook;
  g_onDelay = ilvHttpHook;
  uint32_t s = 0xC0FFEEu;
  int guard = 0;
  bool kilepett = false;
  try {
    while (++guard < 2500) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      g_ilvWhich = (int)(s % 3);
      g_ilvEvery = 1 + (int)((s >> 8) % 9);
      g_ilvLoopBudget = (int)((s >> 16) % 3);
      loop();
    }
  } catch (RestartSignal&)   { kilepett = true; ilvNoteExit(); }
    catch (DeepSleepSignal&) { kilepett = true; ilvNoteExit(); }
  g_onDelay = nullptr; g_onFsWrite = nullptr;
  printf("     [info] %d osszefonas mindket iranyban, %d iteracio, kilepett=%s\n",
         g_ilvHits, guard, kilepett ? "igen" : "nem");
  CHECK(g_ilvHits > 50, "mindket irany tenylegesen osszefonodott");
  CHECK(g_criticalMaxDepth <= 1, "a kritikus szakasz nem agyazodott egymasba");
  CHECK(!g_ilvExitSeen || g_ilvExitLocked || serialHas("FIGYELEM: a mentes"),
        "minden kilepesi ut a zarat megszerezve, vagy kivarva es jelezve tortent");
}

static const uint8_t  PROP_ENDPOINTS         = 5;
static const uint32_t PROP_MAX_RETRY_ROUNDS  = 33;
static const uint32_t PROP_EVLOG_SIZE        = 32;
static const uint32_t PROP_RESET_PULSE_MS    = 90000;
static const char*    PROP_PLAIN_PASS        = "TitkosJelszo123";
static const uint32_t EVLOG_MAGIC_C          = 0x42415A4DUL;

struct PropRnd {
  uint32_t s;
  explicit PropRnd(uint32_t seed) : s(seed ? seed : 1u) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  uint32_t below(uint32_t n) { return n ? (next() % n) : 0u; }
  bool chance(uint32_t percent) { return below(100) < percent; }
};

struct PropState {
  const char* violation = nullptr;
  uint32_t violStep = 0, violMs = 0;
  uint32_t relayMaxHoldMs = 0;
  bool     terminalSeen = false;
  uint32_t reboots = 0, sleeps = 0, restarts = 0;

  uint8_t  maxResetEvents = 0;
  bool     sawResetState = false, sawConfigMode = false, sawFatalMode = false;
  bool     sawRelayPulse = false;
};

static void propViolate(PropState& ps, uint32_t step, const char* what) {
  if (ps.violation == nullptr) {
    ps.violation = what; ps.violStep = step; ps.violMs = g_millis;
  }
}

static void propCheck(PropState& ps, uint32_t step) {
  if (g_relayMaxHighMs > PROP_RESET_PULSE_MS + PROP_RESET_PULSE_MS / 10) {
    propViolate(ps, step, "a rele tovabb maradt behuzva a pulzusnal (a router aram nelkul)");
  }
  if (g_relayMaxHighMs > ps.relayMaxHoldMs) ps.relayMaxHoldMs = g_relayMaxHighMs;
  if (g_relayIsHigh || g_relayMaxHighMs > 0) ps.sawRelayPulse = true;

  if (testState.resetEvents > ps.maxResetEvents) ps.maxResetEvents = testState.resetEvents;
  if (currentState == (State)2) ps.sawResetState = true;
  if (deviceMode == (DeviceMode)1) ps.sawConfigMode = true;
  if (deviceMode == (DeviceMode)2) ps.sawFatalMode = true;

  if (testState.endpointIndex >= PROP_ENDPOINTS) {
    propViolate(ps, step, "endpointIndex tullepte a vegpont-tabla meretet");
  }
  if (rtcRetryRounds > PROP_MAX_RETRY_ROUNDS) {
    propViolate(ps, step, "rtcRetryRounds tullepte a MAX_RETRY_ROUNDS korlatot");
  }

  if (rtcEvMagic == EVLOG_MAGIC_C) {
    const uint32_t db = (rtcEvNext < PROP_EVLOG_SIZE) ? rtcEvNext : PROP_EVLOG_SIZE;
    for (uint32_t i = 0; i < db; i++) {
      const uint8_t c = rtcEvents[i].code;
      if (c < 1 || c > 17) {
        propViolate(ps, step, "ervenytelen esemenykod az esemenynaploban");
        break;
      }
    }
  }

  for (const auto& kv : g_fs) {
    if (kv.second.find(PROP_PLAIN_PASS) != std::string::npos) {
      propViolate(ps, step, "a nyilt szovegu jelszo megjelent a fajlrendszerben");
      break;
    }
  }

  for (const auto& l : g_serialLog) {
    if (l.find(PROP_PLAIN_PASS) != std::string::npos) {
      propViolate(ps, step, "a nyilt szovegu jelszo megjelent a soros kimeneten");
      break;
    }
  }

  if (deviceMode != (DeviceMode)0) {
    ps.terminalSeen = true;
  } else if (ps.terminalSeen) {
    propViolate(ps, step, "MODE_CONFIG/MODE_FATAL utan visszatert MODE_MONITOR-ba ujraindulas nelkul");
  }
}

struct PropEnv {
  bool willConnect = true;
  uint32_t latency = 500;
  std::string encPass;

  uint32_t buttonRate = 0;
  uint32_t netFlapRate = 0;
  uint32_t httpRate = 0;
  uint32_t heapRate = 0;
  uint32_t fsRate = 0;
  bool     netBroken = false;
};

static void propSeedWorld(PropState& ps, const PropEnv& env, bool deepSleepWake) {
  ps.terminalSeen = false;

  coldBoot(env.willConnect, "TestNet", nullptr, "", "", env.latency, deepSleepWake);
  g_fs["/pass.txt"] = env.encPass;
}

static void propRun(uint32_t seed, uint32_t steps) {
  PropRnd r(seed);
  PropState ps;
  PropEnv env;

  char enc[128];
  encodeSecret(PROP_PLAIN_PASS, enc, sizeof(enc));
  env.encPass = enc;
  env.willConnect = r.chance(70);
  env.latency = 100 + r.below(2000);

  static const uint32_t kBtn[]  = { 0, 0, 400000, 60000 };
  static const uint32_t kFlap[] = { 0, 200000, 40000, 8000 };
  env.buttonRate  = kBtn[r.below(4)];
  env.netFlapRate = kFlap[r.below(4)];
  env.httpRate    = 2000 + r.below(60000);
  env.heapRate    = r.chance(50) ? 0u : (20000 + r.below(200000));
  env.fsRate      = r.chance(70) ? 0u : (50000 + r.below(500000));
  env.netBroken   = r.chance(45);
  if (env.netBroken) {
    env.willConnect = true; wifiSim.willConnect = true;
    env.netFlapRate = 0;
    g_httpCode = 503; g_httpBody = "nincs net"; g_httpBeginOk = true;
    pingSim.ok = r.chance(50);
  }

  bool needBoot = true;
  uint32_t step = 0;
  const uint32_t MAX_REBOOTS = 40;

  while (step < steps && ps.violation == nullptr) {
    if (needBoot) {
      if (ps.reboots > MAX_REBOOTS) break;
      propSeedWorld(ps, env, ps.reboots > 0 && r.chance(60));
      try {
        setup();
        needBoot = false;
      } catch (DeepSleepSignal&) {
        propCheck(ps, step);
        if (g_pinState.count(PIN_RELAY) && g_pinState[PIN_RELAY] == HIGH) {
          propViolate(ps, step, "deep sleep behuzott relevel (a router egy alvasnyi ideig aram nelkul)");
        }
        ps.sleeps++; ps.reboots++; step++;
        continue;
      } catch (RestartSignal&) {
        propCheck(ps, step);
        ps.restarts++; ps.reboots++; step++;
        continue;
      }
      ps.reboots++;
    }

    if (env.buttonRate && r.below(env.buttonRate) == 0) {
      const int which = (r.below(2) == 0) ? PIN_RESETBTN : PIN_WIFIBTN;
      g_pinRead[which] = LOW;
      if (g_isr.count(which)) simIsr(which);
    }
    if (env.buttonRate && r.below(env.buttonRate / 2 + 1) == 0) {
      for (int b : { PIN_RESETBTN, PIN_WIFIBTN }) {
        if (g_pinRead[b] == LOW) {
          g_pinRead[b] = HIGH;
          if (g_isr.count(b)) simIsr(b);
        }
      }
    }
    if (env.netFlapRate && r.below(env.netFlapRate) == 0) {
      env.willConnect = !env.willConnect;
      wifiSim.willConnect = env.willConnect;
      if (!env.willConnect && r.chance(20)) wifiSim.authFail = true;
    }
    if (env.httpRate && r.below(env.httpRate) == 0) {
      const bool jo = !env.netBroken && r.chance(60);
      g_httpCode = jo ? 200 : (int)(400 + r.below(200));
      g_httpBody = jo ? "Microsoft NCSI" : "valami mas";
      g_httpBeginOk = jo || r.chance(50);
      pingSim.ok = jo || r.chance(50);
    }
    if (env.heapRate && r.below(env.heapRate) == 0) {
      if (r.chance(50)) {
        g_freeHeap = 8000 + r.below(40000);
        g_maxAllocHeap = g_freeHeap / 2;
      } else {
        g_freeHeap = 180000; g_maxAllocHeap = 110000;
      }
    }
    if (env.fsRate && r.below(env.fsRate) == 0) {
      g_fsWritable = !g_fsWritable;
    }

    try {
      loop();
    } catch (DeepSleepSignal&) {
      propCheck(ps, step);
      if (g_pinState.count(PIN_RELAY) && g_pinState[PIN_RELAY] == HIGH) {
        propViolate(ps, step, "deep sleep behuzott relevel (a router egy alvasnyi ideig aram nelkul)");
      }
      ps.sleeps++; needBoot = true; step++;
      continue;
    } catch (RestartSignal&) {
      propCheck(ps, step);
      ps.restarts++; needBoot = true; step++;
      continue;
    }
    propCheck(ps, step);
    step++;
  }

  printf("     [info] mag %u: %u lepes, %u boot, %u alvas, %u restart, "
         "szimulalt ido %.1f perc\n",
         (unsigned)seed, (unsigned)step, (unsigned)ps.reboots,
         (unsigned)ps.sleeps, (unsigned)ps.restarts, g_millis / 60000.0);
  printf("     [eleres] resetEvents<=%u RESET=%s CONFIG=%s "
         "FATAL=%s rele=%s (leghosszabb pulzus %.1f mp)\n",
         ps.maxResetEvents,
         ps.sawResetState ? "igen" : "NEM", ps.sawConfigMode ? "igen" : "NEM",
         ps.sawFatalMode ? "igen" : "NEM", ps.sawRelayPulse ? "igen" : "NEM",
         ps.relayMaxHoldMs / 1000.0);

  if (ps.violation != nullptr) {
    printf("     [info] a sertes a %u. lepesben, %u ms-nal\n",
           (unsigned)ps.violStep, (unsigned)ps.violMs);
  }
  CHECK(ps.violation == nullptr,
        ps.violation ? ps.violation : "a veletlen bejaras egyetlen tulajdonsagot sem sertett meg");

  CHECK(ps.sawRelayPulse || ps.sawConfigMode || ps.sawFatalMode,
        "a bejaras elerte a program valamelyik erdemi agat (nem ures)");
}

static uint32_t propSteps() {
  const char* s = getenv("RNDSTEPS");
  return s ? (uint32_t)strtoul(s, nullptr, 10) : 400000u;
}
static uint32_t propSeed(uint32_t n) {
  const char* s = getenv("RNDSEED");
  return s ? (uint32_t)strtoul(s, nullptr, 10) + n : (0x9E3779B9u * n + 1u);
}

#define PROP_SCENARIO(n) static void scPROP##n() { propRun(propSeed(n), propSteps()); }
PROP_SCENARIO(1) PROP_SCENARIO(2) PROP_SCENARIO(3) PROP_SCENARIO(4)
PROP_SCENARIO(5) PROP_SCENARIO(6) PROP_SCENARIO(7) PROP_SCENARIO(8)
#undef PROP_SCENARIO

static void scNV20() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);

  g_epochNow = 1780000000UL;
  for (int i = 0; i < 40; i++) logEvent((EventCode)((i % 13) + 1), (uint16_t)i);
  CHECK(saveEventLog("teli naplo"), "a teli naplo mentve");
  EvFileHeader f1; memset(&f1, 0, sizeof(f1));
  CHECK(loadEventLogHeader(f1), "a mentett fajl olvashato");
  CHECK(f1.count == EVLOG_SIZE_C, "32 bejegyzes van benne (a gyuru tele volt)");
  EventEntry regi[EVLOG_SIZE_C];
  CHECK(loadEventLogRange(f1, 0, f1.count, regi), "es a bejegyzesek beolvashatok");

  const std::string flash = g_fs["/evlog.bin"];
  coldBoot(false, "", "", "", "");
  g_fs["/evlog.bin"] = flash;
  setFilesystemReady(true);
  rtcEvMagic = 0;
  g_epochNow = 0;

  logEvent((EventCode)1, 7);
  CHECK(rtcEvNext == 1, "az RTC naploban egyetlen friss bejegyzes van");
  CHECK(saveEventLog("aramszunet utani elso mentes"), "a mentes lefutott");

  EvFileHeader f2; memset(&f2, 0, sizeof(f2));
  CHECK(loadEventLogHeader(f2), "a fajl tovabbra is olvashato");
  printf("     [info] %u -> %u bejegyzes, %u -> %u bajt\n",
         (unsigned)f1.count, (unsigned)f2.count,
         (unsigned)flash.size(), (unsigned)g_fs["/evlog.bin"].size());
  CHECK(f2.count == f1.count + 1,
        "a fajl EGGYEL nott - a tortenet megmaradt, az uj hozzafuzodott");

  EventEntry most[EVFILE_SIZE_C];
  CHECK(loadEventLogRange(f2, 0, f2.count, most), "az uj fajl beolvashato");
  bool egyezik = true;
  for (uint16_t i = 0; i < f1.count; i++) {
    if (memcmp(&most[i], &regi[i], sizeof(EventEntry)) != 0) egyezik = false;
  }
  CHECK(egyezik, "a REGI bejegyzesek valtozatlanul, ugyanabban a sorrendben allnak");
  CHECK(most[f1.count].code == 1 && most[f1.count].param == 7,
        "es a legvegen az aramszunet utani BOOT all");
  CHECK(most[0].epoch >= 1780000000UL,
        "a regi bejegyzesek NTP idobelyege is megmaradt");
}

static void scNV21() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);

  for (int kor = 0; kor < 6; kor++) {
    for (int i = 0; i < EVLOG_SIZE_C; i++) {
      logEvent((EventCode)((kor % 13) + 1), (uint16_t)(kor * 100 + i));
    }
    saveEventLog("kor");
  }
  EvFileHeader fej; memset(&fej, 0, sizeof(fej));
  CHECK(loadEventLogHeader(fej), "a fajl olvashato");
  printf("     [info] 6 x 32 = 192 esemeny utan a fajlban %u bejegyzes van\n",
         (unsigned)fej.count);
  CHECK(fej.count == EVFILE_SIZE_C, "a fajl a gyuru meretenel megall (128)");
  EventEntry e[EVFILE_SIZE_C];
  CHECK(loadEventLogRange(fej, 0, fej.count, e), "beolvashato");

  CHECK(e[EVFILE_SIZE_C - 1].param == 5 * 100 + (EVLOG_SIZE_C - 1),
        "a legvegen a LEGFRISSEBB esemeny all");
  CHECK(e[0].param > 100, "es az elejerol a legregebbiek estek ki");
}

static void scNV22() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  logEvent((EventCode)1, 1);
  CHECK(saveEventLog("elso"), "az elso mentes lefut");
  const std::string elso = g_fs["/evlog.bin"];
  CHECK(!saveEventLog("masodik, uj esemeny nelkul"),
        "uj esemeny nelkul NINCS mentes");
  CHECK(g_fs["/evlog.bin"] == elso, "es a fajl bajtra valtozatlan");

  rtcEvMagic = 0;
  CHECK(!saveEventLog("ures RTC naploval"), "ures RTC naplo nem ir");
  CHECK(g_fs["/evlog.bin"] == elso, "a fajl tovabbra is valtozatlan");
}

static void scCMD1() {
  coldBoot(false, "", "", "", "");
  g_epochNow = 1780000000UL;
  setup();

  for (int i = 0; i < 5; i++) logEvent((EventCode)2, (uint16_t)(500 + i));
  saveEventLog("CMD1");
  for (int i = 0; i < 3; i++) logEvent((EventCode)3, (uint16_t)(600 + i));

  const size_t elotte = g_serialLog.size();
  g_serialIn = "LOG\n";
  loop();
  CHECK(g_serialIn.empty(), "a parancs elfogyott a bemeneti pufferbol");
  CHECK(g_serialLog.size() > elotte, "a parancs kiirt valamit");
  CHECK(serialHas("=== ESEMENYNAPLO ==="), "a naplo fejlece megjelent");
  CHECK(serialHas("-- fajl"), "a FAJL resze megvan");
  CHECK(serialHas("-- RTC (a mentes ota): 3 bejegyzes"),
        "es az RTC reszbol pontosan a mentes ota keletkezett 3");
  CHECK(serialHas("=== NAPLO VEGE ==="), "a kiiras rendesen lezarul");
}

static void scCMD2() {
  coldBoot(false, "", "", "", "");
  setup();

  g_serialIn = "LO";
  loop();
  CHECK(!serialHas("=== ESEMENYNAPLO ==="), "felig beirt parancs nem fut le");
  g_serialIn = "G\n";
  loop();
  CHECK(serialHas("=== ESEMENYNAPLO ==="), "a folytatas utan viszont igen");

  g_serialLog.clear();
  g_serialIn = "MITTUDOMEN\n";
  loop();
  CHECK(serialHas("Ismeretlen parancs"), "ismeretlen parancsra szol");
  CHECK(serialHas("HELP"), "es a HELP-re iranyit");

  g_serialLog.clear();
  g_serialIn = std::string(200, 'X') + "\n";
  loop();
  CHECK(!serialHas("Ismeretlen parancs"),
        "a tul hosszu sor eldobodik, NEM lesz belole csonkolt parancs");
  CHECK(serialHas("Tul hosszu parancs, eldobva"), "es ezt ki is mondja");
  g_serialIn = "HELP\n";
  loop();
  CHECK(serialHas("Parancsok:"), "es utana a kovetkezo parancs mukodik");

  g_serialLog.clear();
  g_serialIn = "log\n";
  loop();
  CHECK(serialHas("=== ESEMENYNAPLO ==="), "a parancs kis-nagybetu fuggetlen");
}

static void scCMD3() {
  coldBoot(false, "", "", "", "");
  setup();
  enterFatal("teszt");
  CHECK(deviceMode == (DeviceMode)2, "MODE_FATAL");
  g_serialLog.clear();
  g_serialIn = "LOG\n";
  loop();
  CHECK(serialHas("=== ESEMENYNAPLO ==="),
        "a LOG parancs MODE_FATAL-ban is kiszolgalodik");
}

static void scCOV7() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  logEvent((EventCode)1, 1);
  g_serialIn = "LOG\n";
  loop();
  CHECK(serialHas("-- fajl: nincs, vagy nem olvashato"),
        "fajl nelkul a LOG kimondja, hogy nincs fajl");
  CHECK(serialHas("-- RTC"), "es az RTC reszt attol meg kiirja");

  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  for (int i = 0; i < 10; i++) logEvent((EventCode)2, (uint16_t)i);
  CHECK(saveEventLog("COV7"), "van mentett naplo");
  g_serialLog.clear();
  g_fsShortRead = true; g_fsShortReadSkip = 3;
  g_serialIn = "LOG\n";
  loop();
  g_fsShortRead = false; g_fsShortReadSkip = 0;
  CHECK(serialHas("(a fajl olvasasa megszakadt)"),
        "a felbeszakadt olvasast a LOG kimondja");
  CHECK(serialHas("=== NAPLO VEGE ==="), "es a kiiras attol meg lezarul");

  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  for (int i = 0; i < 10; i++) logEvent((EventCode)2, (uint16_t)i);
  CHECK(saveEventLog("elozmeny"), "van elozmeny a fajlban");
  logEvent((EventCode)3, 99);
  g_serialLog.clear();

  g_fsShortRead = true; g_fsShortReadSkip = 2;
  g_onFsWrite = []() { g_fsShortRead = false; g_fsShortReadSkip = 0; };
  const bool mentett = saveEventLog("serult elozmennyel");
  g_onFsWrite = nullptr;
  g_fsShortRead = false; g_fsShortReadSkip = 0;
  CHECK(serialHas("a korabbi naplofajl nem olvashato"),
        "a mentes szol, ha az elozmeny olvashatatlan");
  CHECK(mentett, "de az UJ bejegyzeseket ettol meg elmenti");

  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  for (int i = 0; i < 5; i++) logEvent((EventCode)2, (uint16_t)i);
  saveEventLog("range");
  EvFileHeader fej; memset(&fej, 0, sizeof(fej));
  CHECK(loadEventLogHeader(fej), "van fajl");
  EventEntry ki[8];
  CHECK(!loadEventLogRange(fej, 0, 0, ki), "nulla darab -> elutasitva");
  CHECK(!loadEventLogRange(fej, fej.count, 1, ki), "a fajl vege utan -> elutasitva");
  CHECK(!loadEventLogRange(fej, 1, (uint16_t)fej.count, ki),
        "a fajlbol kilogo szakasz -> elutasitva");
  CHECK(loadEventLogRange(fej, 1, (uint16_t)(fej.count - 1), ki),
        "a pontosan a vegeig tarto szakasz viszont jo");

  EvFileHeader rossz = fej;
  rossz.count = 0;
  CHECK(!loadEventLogRange(rossz, 0, 1, ki), "count = 0 -> elutasitva");
  rossz.count = (uint16_t)(EVFILE_SIZE_C + 1);
  CHECK(!loadEventLogRange(rossz, 0, 1, ki), "count > fajlgyuru -> elutasitva");

  setFilesystemReady(false);
  CHECK(!loadEventLogRange(fej, 0, 1, ki), "fajlrendszer nelkul -> hamis");
  setFilesystemReady(true);
  g_fsReadable = false;
  CHECK(!loadEventLogRange(fej, 0, 1, ki), "nem nyithato fajl -> hamis");
  CHECK(!loadEventLogRange(fej, 0, fej.count, ki), "a teljes betoltes is hamisat ad");
  g_fsReadable = true;

  g_fsShortRead = true; g_fsShortReadSkip = 1;
  CHECK(!loadEventLogRange(fej, 2, 1, ki),
        "a kihagyas kozben megszakadt olvasas -> hamis, nem szemet");
  g_fsShortRead = false; g_fsShortReadSkip = 0;

  g_fsShortRead = true; g_fsShortReadSkip = 0;
  CHECK(!loadEventLogRange(fej, 0, fej.count, ki), "a fejlec olvasasa elbukik -> hamis");
  g_fsShortRead = false;
}

static void scCOV8() {
  coldBoot(false, "", "", "", "");
  setup();
  g_serialLog.clear();
  g_serialReadLies = true;
  g_serialIn = "LOG\n";
  loop();
  g_serialReadLies = false;
  CHECK(!serialHas("=== ESEMENYNAPLO ==="),
        "a hazudos available() nem futtat parancsot");

  loop();
  CHECK(serialHas("=== ESEMENYNAPLO ==="), "utana viszont lefut");
}

static void scGAP() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  startConfigPortal();
  for (int i = 0; i < 10; i++) logEvent((EventCode)2, (uint16_t)(100 + i));
  CHECK(saveEventLog("mentes"), "10 bejegyzes mentve");
  const uint32_t mentettEddig = rtcSavedEvNext;
  CHECK(mentettEddig >= 10, "a jelzo szerint mentettunk");
  for (int i = 0; i < 3; i++) logEvent((EventCode)3, (uint16_t)(200 + i));

  g_fs.erase("/evlog.bin");
  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  size_t rtcSor = 0, p = 0;
  while ((p = b.find("<td>RTC</td>", p)) != std::string::npos) { rtcSor++; p++; }
  printf("     [info] az RTC-ben %u bejegyzes van, a lap %zu RTC-sort mutat\n",
         (unsigned)rtcEvNext, rtcSor);
  CHECK(b.find("<td>100</td>") != std::string::npos,
        "a fajl elvesztese utan a REGEBBI RTC bejegyzesek is latszanak");
  CHECK(rtcSor == rtcEvNext, "mind a meglevo RTC bejegyzes megjelenik");

  g_serialLog.clear();
  printEventLogs();
  CHECK(serialHas("-- fajl: nincs, vagy nem olvashato"), "a LOG is latja, hogy nincs fajl");
  char vart[48];
  snprintf(vart, sizeof(vart), "-- RTC (a mentes ota): %u bejegyzes", (unsigned)rtcEvNext);
  CHECK(serialHas(vart), "es a soros kiiras is a TELJES RTC naplot adja");
}

static void scGAP2() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  startConfigPortal();
  for (int i = 0; i < 20; i++) logEvent((EventCode)2, (uint16_t)(300 + i));
  CHECK(saveEventLog("elozo elet"), "20 bejegyzes mentve");
  EvFileHeader f; memset(&f, 0, sizeof(f));
  CHECK(loadEventLogHeader(f), "a fajl olvashato");
  CHECK(f.evNextAtSave >= 20, "a fajl 20-ig tart");

  const std::string flash = g_fs["/evlog.bin"];
  coldBoot(false, "", "", "", "");
  g_fs["/evlog.bin"] = flash;
  setFilesystemReady(true);
  startConfigPortal();
  rtcEvMagic = 0;
  logEvent((EventCode)1, 42);
  CHECK(rtcEvNext == 1, "az RTC-ben egyetlen friss bejegyzes");
  CHECK(rtcSavedEvNext == 0, "es a 'mar mentettuk' jelzo nullarol indul");

  AsyncWebServerRequest req; g_handlers["/log#1"](&req);
  const std::string& b = req._body;
  size_t rtcSor = 0, fajlSor = 0, p = 0;
  while ((p = b.find("<td>RTC</td>", p)) != std::string::npos) { rtcSor++; p++; }
  p = 0;
  while ((p = b.find("<td>fajl</td>", p)) != std::string::npos) { fajlSor++; p++; }
  printf("     [info] %zu fajl-sor + %zu RTC-sor\n", fajlSor, rtcSor);
  CHECK(rtcSor == 1, "a friss bejegyzes NEM veszik el (a fajl szama nem nyomja el)");
  CHECK(b.find("<td>42</td>") != std::string::npos, "es tenyleg ott is van");
  CHECK(fajlSor == 20, "a fajl 20 sora is megvan - ketszerezes nelkul");
}

static EventEntry g_rstMentes[EVLOG_SIZE_C];
static void szoftveresReboot(const std::string& flash) {
  const uint32_t evMagic = rtcEvMagic, evNext = rtcEvNext, saved = rtcSavedEvNext;
  memcpy(g_rstMentes, rtcEvents, sizeof(g_rstMentes));
  const uint32_t wdtMagic = rtcWdtMagic, wdtResets = rtcWdtResets;
  coldBoot(false, "", "", "", "");
  g_fs["/evlog.bin"] = flash;
  setFilesystemReady(true);
  rtcEvMagic = evMagic; rtcEvNext = evNext; rtcSavedEvNext = saved;
  memcpy(rtcEvents, g_rstMentes, sizeof(g_rstMentes));
  rtcWdtMagic = wdtMagic; rtcWdtResets = wdtResets;
}

static void scRST1() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  CHECK(deviceMode == (DeviceMode)0, "MODE_MONITOR - itt engedett az ujraindulas");
  for (int i = 0; i < 6; i++) logEvent((EventCode)2, (uint16_t)(400 + i));
  CHECK(saveEventLog("alap"), "van mentett elozmeny");
  const std::string flash = g_fs["/evlog.bin"];
  const uint32_t mentettDb = rtcSavedEvNext;

  g_freeHeap = 8000; g_maxAllocHeap = 4000;
  bool ujraindult = false;
  for (int i = 0; i < 8 && !ujraindult; i++) {
    g_millis += 11000;
    try { checkHeap(g_millis); } catch (RestartSignal&) { ujraindult = true; }
  }
  CHECK(ujraindult, "a heap-vedelem tenyleg ujrainditott");
  const uint32_t evNextElott = rtcEvNext;
  const uint32_t ujDb = evNextElott - mentettDb;
  printf("     [info] az ujrainditas elott %u uj bejegyzes keletkezett\n", (unsigned)ujDb);
  CHECK(ujDb > 0, "es kozben naplozott is (LOW HEAP / HEAP RESTART)");

  const std::string flashUtan = g_fs["/evlog.bin"];
  CHECK(flashUtan != flash, "a fajl MAR az ujraindulas elott frissult");
  CHECK(rtcSavedEvNext == evNextElott,
        "es a 'meddig mentettuk' jelzo is elorelepett - minden mentve");

  szoftveresReboot(flashUtan);
  g_freeHeap = 180000; g_maxAllocHeap = 110000;
  CHECK(rtcEvNext == evNextElott, "az RTC naplo szamlaloja tulelte az ujraindulast");

  bool vanHeapRestart = false, vanLowHeap = false;
  for (uint32_t i = mentettDb; i < evNextElott; i++) {
    const uint8_t c = rtcEvents[i % EVLOG_SIZE_C].code;
    if (c == 14) vanHeapRestart = true;
    if (c == 13) vanLowHeap = true;
  }
  CHECK(vanHeapRestart, "a HEAP RESTART bejegyzes tulelte az ujraindulast");
  CHECK(vanLowHeap, "a LOW HEAP is megvan");

  EvFileHeader f; memset(&f, 0, sizeof(f));
  CHECK(loadEventLogHeader(f), "a fajl olvashato");
  printf("     [info] a fajlban %u bejegyzes (mentve volt %u, uj %u)\n",
         (unsigned)f.count, (unsigned)mentettDb, (unsigned)ujDb);
  CHECK(f.count == mentettDb + ujDb,
        "a fajl a regi ES az uj bejegyzeseket is tartalmazza - egy sem veszett el");

  const std::string flashVegleges = g_fs["/evlog.bin"];
  coldBoot(false, "", "", "", "");
  g_fs["/evlog.bin"] = flashVegleges;
  setFilesystemReady(true);
  rtcEvMagic = 0;
  EvFileHeader f2; memset(&f2, 0, sizeof(f2));
  CHECK(loadEventLogHeader(f2), "aramszunet utan is olvashato a fajl");
  CHECK(f2.count == evNextElott,
        "es a HEAP RESTART bizonyiteka az aramszunetet is tuleli");
}

static void scRST2() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  for (int i = 0; i < 4; i++) logEvent((EventCode)2, (uint16_t)(500 + i));
  CHECK(saveEventLog("alap"), "van mentett elozmeny");
  const std::string flash = g_fs["/evlog.bin"];
  const uint32_t mentettDb = rtcSavedEvNext;

  logEvent((EventCode)3, 601);
  logEvent((EventCode)3, 602);
  const uint32_t evNextElott = rtcEvNext;

  szoftveresReboot(flash);
  g_resetReason = ESP_RST_TASK_WDT;
  checkWatchdogResets();
  CHECK(rtcEvNext == evNextElott + 1, "a WDT RESET bejegyzes hozzaadodott");
  CHECK(rtcSavedEvNext == mentettDb, "a 'meddig mentettuk' jelzo valtozatlan");

  bool van601 = false, van602 = false, vanWdt = false;
  for (uint32_t i = mentettDb; i < rtcEvNext; i++) {
    const EventEntry& e = rtcEvents[i % EVLOG_SIZE_C];
    if (e.param == 601) van601 = true;
    if (e.param == 602) van602 = true;
    if (e.code == 10) vanWdt = true;
  }
  CHECK(van601 && van602,
        "a mentes UTAN, a lefagyas ELOTT keletkezett bejegyzesek megvannak");
  CHECK(vanWdt, "es az uj WDT RESET bejegyzes is ott van");

  CHECK(saveEventLog("lefagyas utan"), "a kovetkezo mentes lefut");
  EvFileHeader f; memset(&f, 0, sizeof(f));
  CHECK(loadEventLogHeader(f), "a fajl olvashato");
  printf("     [info] a fajlban %u bejegyzes (mentve volt %u, mentetlen %u)\n",
         (unsigned)f.count, (unsigned)mentettDb, (unsigned)(rtcEvNext - mentettDb));
  CHECK(f.count == rtcEvNext, "mind a mentetlen bejegyzes a fajlba kerult");
}

static void scRST3() {
  coldBoot(false, "", "", "", "");
  setFilesystemReady(true);
  for (int i = 0; i < 5; i++) logEvent((EventCode)2, (uint16_t)(700 + i));
  CHECK(saveEventLog("alap"), "van mentett elozmeny");
  const std::string flash = g_fs["/evlog.bin"];
  const uint32_t mentettDb = rtcSavedEvNext;

  g_freeHeap = 8000; g_maxAllocHeap = 4000;
  g_fsSilentWriteFail = true;
  bool ujraindult = false;
  for (int i = 0; i < 8 && !ujraindult; i++) {
    g_millis += 11000;
    try { checkHeap(g_millis); } catch (RestartSignal&) { ujraindult = true; }
  }
  g_fsSilentWriteFail = false;
  printf("     [info] sikertelen mentes mellett is ujraindult: %s\n",
         ujraindult ? "igen" : "NEM");
  CHECK(ujraindult, "a sikertelen mentes NEM akadalyozza meg az ujraindulast");
  CHECK(serialHas("NEM sikerult") || serialHas("verify FAILED")
        || serialHas("rovid iras"), "es a kudarcot ki is mondja");
  CHECK(rtcSavedEvNext == mentettDb,
        "a jelzo NEM lep elore - a bejegyzesek 'mentetlennek' maradnak");
  CHECK(rtcEvNext > mentettDb, "az RTC naplo tovabbra is ep, a bejegyzesek benne");

  endConfigWrite();
  g_freeHeap = 180000; g_maxAllocHeap = 110000;
  g_fs["/evlog.bin"] = flash;
  const uint32_t evNextMost = rtcEvNext;
  CHECK(saveEventLog("potlas"), "a kesobbi mentes lefut");
  EvFileHeader f; memset(&f, 0, sizeof(f));
  CHECK(loadEventLogHeader(f), "a fajl olvashato");
  CHECK(f.count == evNextMost, "es MINDEN bejegyzes bekerult - semmi nem veszett el");
}

static int g_wearWrites = 0;
static void wearHook() { g_wearWrites++; }

static void scWEAR() {
  coldBoot(true, "TestNet", "pw", "", "");
  setFilesystemReady(true);
  g_httpBody = "Microsoft Connect Test";
  g_wearWrites = 0; g_onFsWrite = wearHook;
  setup();
  const uint32_t kezdet = g_millis;
  int guard = 0;
  try { while (++guard < 400000) loop(); } catch (...) {}
  g_onFsWrite = nullptr;
  const double percek = (g_millis - kezdet) / 60000.0;
  printf("     [info] mukodo internet: %.1f perc alatt %d flash-iras\n",
         percek, g_wearWrites);
  CHECK(percek > 30.0, "a meres tenyleg hosszu futast fedett le");
  CHECK(deviceMode == (DeviceMode)0, "es az eszkoz vegig monitor modban volt");
  CHECK(g_wearWrites == 0,
        "MUKODO INTERNET MELLETT NULLA flash-iras - a flash nem kopik");

  coldBoot(true, "TestNet", "pw", "", "");
  setFilesystemReady(true);
  g_httpCode = 503; g_httpBody = "nincs net"; pingSim.ok = false;
  g_wearWrites = 0; g_onFsWrite = wearHook;
  try { setup(); } catch (...) {}
  const uint32_t t0 = g_millis;
  guard = 0;
  int resetek = 0; size_t nezett = 0;
  try {
    while (++guard < 20000000 && g_millis - t0 < 24u * 3600 * 1000) {
      loop();
      for (size_t i = nezett; i < g_log.size(); i++) if (g_log[i] == RELAY_HIGH) resetek++;
      nezett = g_log.size();
    }
  } catch (...) {}
  g_onFsWrite = nullptr;
  printf("     [info] halott internet egy napig: %d flash-iras, %d reset\n", g_wearWrites, resetek);
  CHECK(resetek > 0 && g_wearWrites > 0, "a hibauton viszont menti a naplot");
  CHECK(g_wearWrites <= 2 * resetek + 2, "korlatosan: resetenkent egy mentes (2 iras)");
}







static void scSERlog() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  g_serialIn = "/log\n";
  loop();
  CHECK(serialHas("=== ESEMENYNAPLO ==="), "a /log is kiirja a naplot");
  CHECK(!serialHas("Ismeretlen parancs"), "nem ismeretlen parancs");
}



static void scHC1() {
  CHECK(httpCodeLogParam(200) == 200, "200 valtozatlan");
  CHECK(httpCodeLogParam(204) == 204, "204 valtozatlan");
  CHECK(httpCodeLogParam(403) == 403, "403 valtozatlan");
  CHECK(httpCodeLogParam(-1) == 1001, "-1 -> 1001");
  CHECK(httpCodeLogParam(-11) == 1011, "-11 -> 1011");
  CHECK(httpCodeLogParam(-100) == 1100, "a begin hiba -> 1100");
  CHECK(httpCodeLogParam(70000) == 65535, "tulnagy pozitiv: telitodik");
  CHECK(httpCodeLogParam(-70000) == 65535, "tulnagy negativ: telitodik");

  coldBoot(true, "TestNet", "pw", "", "");
  g_httpCode = -11;
  testInternetHTTP("http://x/", "valami");
  CHECK(lastHttpCode() == -11, "a GET kodja megmarad");
  g_httpBeginOk = false;
  testInternetHTTP("http://x/", "valami");
  CHECK(lastHttpCode() == -100, "a begin hiba sajat kodot kap");
}

static void scHC2() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();
  ujNaplo();
  g_httpCode = -11; pingSim.ok = false;
  futtat(3u * 60 * 1000, [] { return testState.outage; });
  CHECK(evCount(EVC_TEST_FAIL, 1) == 1, "TEST FAIL 1");
  CHECK(evCount(EVC_HTTP_ERROR, 1011) == 1, "HTTP ERROR 1011 (idotullepes)");
  CHECK(rtcEvents[(rtcEvNext - 1) % 32].code == EVC_HTTP_ERROR,
        "a kod a TEST FAIL utani bejegyzes");
}


static void scFB1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(wifiSim.staDns[2].str() == "1.1.1.1", "DHCP: a tartalek DNS 1.1.1.1");
  CHECK(wifiSim.staDns[0].isZero() && wifiSim.staDns[1].isZero(),
        "a router DNS-et (0, 1) nem irja felul");

  monitorUzembe();
  const int elotte = wifiSim.dnsSetCount;
  pingSim.ok = false;
  futtat(2u * 60 * 1000, [] { return serialHas("HTTP teszt:"); });
  CHECK(wifiSim.dnsSetCount > elotte, "a HTTP teszt elott is beallitja");

  coldBoot(true, "TestNet", "pw", "192.168.1.200", "192.168.1.1");
  setup();
  CHECK(wifiSim.dnsSetCount == 0, "statikus IP mellett nem allit kulon DNS-t");
  CHECK(wifiSim.cfgDns2.str() == "1.1.1.1", "ott a config() dns2-je a tartalek");
}

static void scSL1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(!wifiSim.sleepEnabled, "a Wi-Fi modem-alvas kikapcsolva");
  CHECK(logIndex("WiFi.setSleep(false)") >= 0
        && logIndex("WiFi.setSleep(false)") < logIndex("WiFi.begin"),
        "a WiFi.begin() elott");
}

static void scWL1() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();
  futtat(60u * 1000, [] { return serialHas("Successful Test"); });
  g_log.clear();
  wifiSim.authFail = true; wifiSim.willConnect = false; wifiSim.begun = false;
  futtat(30u * 60 * 1000, [] { return deviceMode != (DeviceMode)0 || relayPulses() > 0; });
  CHECK(deviceMode == (DeviceMode)1, "AP beallito modba ment");
  CHECK(relayPulses() == 0, "a routert NEM aramtalanitotta");
  CHECK(serialHas("router reset helyett AP mod"), "es megmondja, miert");
}

static void scWL2() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();
  futtat(60u * 1000, [] { return serialHas("Successful Test"); });
  g_log.clear();
  wifiSim.willConnect = false; wifiSim.begun = false;
  futtat(30u * 60 * 1000, [] { return relayPulses() > 0; });
  CHECK(relayPulses() == 1, "a router reset megtortent");
  CHECK(deviceMode == (DeviceMode)0, "monitor modban maradt");
}

static void scSP1() {
  auto ip = [](const char* s) { IPAddress a; a.fromString(s); return a; };
  CHECK(staticPairValid(ip("192.168.1.50"), ip("192.168.1.1")), "ugyanaz a /24 -> jo");
  CHECK(!staticPairValid(ip("192.168.1.50"), ip("192.168.0.1")), "mas /24 -> rossz");
  CHECK(!staticPairValid(ip("10.0.0.5"), ip("192.168.1.1")), "teljesen mas halozat -> rossz");
  CHECK(!staticPairValid(ip("192.168.1.1"), ip("192.168.1.1")), "az eszkoz nem lehet a gateway");
  CHECK(!staticPairValid(ip("192.168.1.0"), ip("192.168.1.1")), "halozati cim -> rossz");
  CHECK(!staticPairValid(ip("192.168.1.255"), ip("192.168.1.1")), "broadcast cim -> rossz");
  CHECK(!staticPairValid(ip("192.168.1.50"), ip("192.168.1.255")), "broadcast gateway -> rossz");
  CHECK(!staticPairValid(ip("192.168.1.50"), ip("192.168.1.0")), "halozati gateway -> rossz");
  CHECK(!staticPairValid(IPAddress(), ip("192.168.1.1")), "0.0.0.0 -> rossz");
}

static void scSP2() {
  coldBoot(true, "TestNet", "pw", "192.168.1.50", "192.168.0.1");
  setup();
  CHECK(wifiSim.configCount == 0, "nem allit be statikus IP-t");
  CHECK(!staticConfigActive, "DHCP");
  CHECK(serialHas("nem illik ossze"), "es kiirja, miert");
  CHECK(deviceMode == (DeviceMode)0, "attol meg csatlakozik");
}

static void scSP3() {
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("MyNetwork", "titkosjelszo", "192.168.1.50", "192.168.0.1", &body);
  CHECK(code != 200, "elutasitja");
  CHECK(body.find("nem illik ossze") != std::string::npos, "es megmondja, miert");
  CHECK(!g_fs.count("/ip.txt") || g_fs["/ip.txt"].empty(), "az IP nem kerult a flashbe");
  const int ok = postConfig("MyNetwork", "titkosjelszo", "192.168.1.50", "192.168.1.1");
  CHECK(ok == 200, "egy osszeillo par viszont mentheto");
}

static void scPW1() {
  coldBoot(true, "TestNet", " titok jelszo ", "", "");
  setup();
  CHECK(wifiSim.lastPass == " titok jelszo ", "a vezeto/zaro szokoz megmaradt");

  coldBoot(true, "TestNet", "jelszo123\r\n", "", "");
  setup();
  CHECK(wifiSim.lastPass == "jelszo123", "a CR/LF lekerult");

  char enc[160];
  encodeSecret("ab cd", enc, sizeof(enc));
  coldBoot(true, "TestNet", enc, "", "");
  setup();
  CHECK(wifiSim.lastPass == "ab cd", "a kodolt jelszo is helyes");

  char out[40];
  g_fs["/x.txt"] = "  ertek  ";
  readConfigValue(LittleFS, "/x.txt", out, sizeof(out));
  CHECK(std::string(out) == "ertek", "a tobbi mezo tovabbra is vagodik");
  readSecretValue(LittleFS, "/x.txt", out, sizeof(out));
  CHECK(std::string(out) == "  ertek  ", "a titok nem");
}

static void scPG2() {
  coldBoot(true, "TestNet", "pw", "", "");
  pingSim.phantomSuccess = true;
  CHECK(!testInternetPing(IPAddress(1, 1, 1, 1), "teszt"), "a fantom-siker bukasnak szamit");
  pingSim.phantomSuccess = false;
  CHECK(testInternetPing(IPAddress(1, 1, 1, 1), "teszt"), "a valodi valasz tovabbra is siker");
}

static const char* kApAbc = "abcdefghjkmnpqrstuvwxyz23456789A";

static void scAPP1() {
  coldBoot(false, "", "", "", "");
  setup();
  const std::string pw = wifiSim.apPass;
  printf("     [info] generalt AP jelszo: %s\n", pw.c_str());
  CHECK(pw.size() == 12, "12 karakter");
  CHECK(pw != "12345678", "nem a regi univerzalis jelszo");
  CHECK(pw.find_first_not_of(kApAbc) == std::string::npos,
        "csak az osszetevesztes-mentes abece betui");
  CHECK(logIndex("WiFi.mode(AP)") >= 0 && logIndex("WiFi.mode(AP)") < logIndex("esp_random"),
        "a veletlen a radio bekapcsolasa UTAN keszul (csak ugy valodi veletlen)");
  CHECK(g_fs.count("/appass.txt") == 1, "elmentette");
  CHECK(g_fs["/appass.txt"].find(pw) == std::string::npos, "kodolva, nem nyilt szovegkent");
  CHECK(serialHas(("UJ AP jelszo generalva (irdd fel az eszkozre!): " + pw).c_str()),
        "a soros porton kiirja, hogy fel lehessen irni");
}

static void scAPP2() {
  coldBoot(false, "", "", "", "");
  setup();
  const std::string pw = wifiSim.apPass;
  const std::string fajl = g_fs["/appass.txt"];
  coldBoot(false, "", "", "", "");
  g_fs["/appass.txt"] = fajl;
  setup();
  CHECK(wifiSim.apPass == pw, "ugyanaz a jelszo");
  CHECK(g_randomCalls == 0, "nem generalt ujat");
  CHECK(serialHas(("AP beallito portal jelszava: " + pw).c_str()), "indulaskor kiirja");

  coldBoot(true, "TestNet", "pw", "", "");
  g_fs["/appass.txt"] = fajl;
  setup();
  CHECK(serialHas(("AP beallito portal jelszava: " + pw).c_str()), "normal indulaskor is kiirja");
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_fs.count("/appass.txt") == 0 && g_randomCalls == 0,
        "normal uzemben nem general es nem ir flasht");
}

static void scAPP3() {
  coldBoot(false, "", "", "", "");
  setup();
  const std::string fajl = g_fs["/appass.txt"];
  try { doWifiReset(); } catch (RestartSignal&) {}
  CHECK(g_fs["/appass.txt"] == fajl, "az AP jelszo megmaradt");
}

static void scAPP4() {
  coldBoot(false, "", "", "", "");
  g_randomState = 0x1111u; g_efuseMac = 0x0000A1B2C3D4E5F6ULL;
  setup();
  const std::string pw1 = wifiSim.apPass;
  const bool nev1 = logIndex("WiFi.softAP(ESP-ESP32-C3-B2A1)") >= 0;
  coldBoot(false, "", "", "", "");
  g_randomState = 0x2222u; g_efuseMac = 0x0000112233445566ULL;
  setup();
  CHECK(pw1 != wifiSim.apPass, "mas eszkoz, mas jelszo");
  CHECK(nev1, "az AP neve a MAC utolso ket bajtjat viseli (B2A1)");
  CHECK(logIndex("WiFi.softAP(ESP-ESP32-C3-2211)") >= 0, "a masik eszkoze mas (2211)");
  g_efuseMac = 0x0000A1B2C3D4E5F6ULL;
}

static void scAPP5() {
  coldBoot(false, "", "", "", "");
  char enc[64]; encodeSecret("rovid", enc, sizeof(enc));
  g_fs["/appass.txt"] = enc;
  setup();
  CHECK(wifiSim.apPass.size() == 12, "uj, ervenyes jelszo");
  CHECK(wifiSim.softApCount == 1, "az AP elindult");
}

static void scAPP6() {
  coldBoot(false, "", "", "", "");
  g_fsWritable = false;
  setup();
  CHECK(wifiSim.softApCount == 1 && wifiSim.apPass.size() == 12, "az AP elindult");
  CHECK(serialHas("az AP jelszo mentese nem sikerult"), "figyelmeztet");
  CHECK(!configWriteInProgress(), "a zarat elengedte");
}


// Perc szerinti kornyezet: megy-e a HTTP, a router (gateway) pingje es az internet pingje.
struct FtPerc { bool httpOk; bool gwOk; bool inetOk; };
template <class M>
static std::vector<double> ftJatszik(uint32_t percek, M menetrend) {
  std::vector<double> resetek;
  const uint32_t t0 = g_millis;
  size_t nezett = g_log.size();
  int guard = 0;
  try {
    while (g_millis - t0 < percek * 60000u && ++guard < 20000000) {
      const FtPerc a = menetrend((g_millis - t0) / 60000u);
      g_httpAutoOk = a.httpOk;
      if (!a.httpOk) g_httpCode = -11;
      pingSim.perTarget["192.168.1.1"] = a.gwOk;
      pingSim.perTarget["1.1.1.1"] = a.inetOk;
      pingSim.perTarget["8.8.8.8"] = a.inetOk;
      loop();
      for (size_t i = nezett; i < g_log.size(); i++)
        if (g_log[i] == RELAY_HIGH) resetek.push_back((g_millis - t0) / 60000.0);
      nezett = g_log.size();
    }
  } catch (DeepSleepSignal&) {}
  return resetek;
}

static void ftKiir(const char* mi, const std::vector<double>& r) {
  printf("     [info] %s: %zu reset (perc:", mi, r.size());
  for (double x : r) printf(" %.1f", x);
  printf(")\n");
}

// Mukodo halozattal indul, egy sikeres ellenorzessel.
static void ftIndit() {
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  monitorUzembe();
  ftJatszik(2, [](uint32_t) { return FtPerc{ true, true, true }; });
  g_log.clear(); g_serialLog.clear(); ujNaplo();
  g_httpFailMs = 10000; g_httpAutoOk = false;
}

static void scRD1() {
  // Router hiba: sem a router, sem az internet nem valaszol -> reset kb. 2 perc utan.
  ftIndit();
  auto r = ftJatszik(20, [](uint32_t) { return FtPerc{ false, false, false }; });
  ftKiir("router hiba 20 percen at", r);
  CHECK(r.size() == 1, "egy reset");
  CHECK(r.size() == 1 && r[0] >= 2.0 && r[0] < 4.5, "kb. 2,5 perccel az elso hibas ellenorzes utan");
  CHECK(evCount(EVC_NET_DIAG, 1) == 1 && evCount(EVC_NET_DIAG) == 1, "NET DIAG 1 (a router sem valaszol)");
  CHECK(evCount(EVC_TEST_FAIL) == 1, "a kieses kezdete egyszer naplozva");
}

static void scRD2() {
  // Rovid szolgaltatoi kiesesek (a router valaszol): 5 es 10 perc -> nincs reset.
  for (uint32_t hossz : { 5u, 10u, 14u }) {
    ftIndit();
    auto r = ftJatszik(40, [hossz](uint32_t p) {
      const bool rossz = p >= 5 && p < 5 + hossz;
      return FtPerc{ !rossz, true, !rossz }; });
    printf("     [info] %u perces szolgaltatoi kieses: %zu reset\n", hossz, r.size());
    CHECK(r.empty(), hossz == 5 ? "5 perc: nincs reset" : hossz == 10 ? "10 perc: nincs reset"
                                                        : "14 perc: nincs reset");
  }
}

static void scRD3() {
  // 6 oras szolgaltatoi kieses: 15 perc utan reset, majd 30 perc, 1 ora, 2 ora szunettel.
  ftIndit();
  auto r = ftJatszik(360, [](uint32_t) { return FtPerc{ false, true, false }; });
  ftKiir("6 oras szolgaltatoi kieses", r);
  CHECK(r.size() == 4, "negy reset");
  if (r.size() == 4) {
    CHECK(r[0] >= 15.0 && r[0] < 17.5, "az elso 15 perc utan");
    CHECK(r[1] - r[0] >= 30.0 && r[1] - r[0] < 31.5, "a masodik 30 perccel kesobb");
    CHECK(r[2] - r[1] >= 60.0 && r[2] - r[1] < 61.5, "a harmadik 1 oraval kesobb");
    CHECK(r[3] - r[2] >= 120.0 && r[3] - r[2] < 121.5, "a negyedik 2 oraval kesobb");
  }
  CHECK(evCount(EVC_NET_DIAG, 2) == 4, "mind NET DIAG 2 (a router valaszol)");
}

static void scRD4() {
  // A 2026-11-22-i terepi nap: router es internet ping megy, a HTTP negyszer bukik.
  ftIndit();
  const struct { uint32_t kezd, hossz; } zavar[] = { { 10, 8 }, { 290, 9 }, { 297, 4 }, { 305, 6 } };
  auto r = ftJatszik(360, [&zavar](uint32_t p) {
    bool rossz = false;
    for (auto& z : zavar) if (p >= z.kezd && p < z.kezd + z.hossz) rossz = true;
    return FtPerc{ !rossz, true, true }; });
  printf("     [info] a terepi nap: %zu reset (a regi firmware: 4)\n", r.size());
  CHECK(r.empty(), "nulla router reset");
  CHECK(evCount(EVC_TEST_FAIL) == 0, "kiesest sem naplozott: a ping valaszolt");
  CHECK(g_httpUrls.empty(), "HTTP kerest sem kuldott (a ping eleg volt)");
}

static void scRD5() {
  // Pingre nem valaszolo router, szolgaltatoi kieses: router hibanak latszik (ismert ar).
  ftIndit();
  auto r = ftJatszik(30, [](uint32_t) { return FtPerc{ false, false, false }; });
  CHECK(r.size() == 1 && r[0] < 4.5, "a 15 helyett kb. 2 perc utan resetel");
  CHECK(evCount(EVC_NET_DIAG, 1) == 1, "NET DIAG 1");
}

static void scRD6() {
  // A halozat szuri a pinget, a HTTP megy: van internet, nincs reset.
  ftIndit();
  g_httpUrls.clear();
  auto r = ftJatszik(120, [](uint32_t) { return FtPerc{ true, false, false }; });
  CHECK(r.empty(), "nincs reset");
  CHECK(evCount(EVC_TEST_FAIL) == 0, "kiesest sem naplozott");
  std::set<std::string> kulonbozo(g_httpUrls.begin(), g_httpUrls.end());
  CHECK(kulonbozo.size() == 5, "a tartalek HTTP teszt mind az ot vegpontot sorra veszi");
}

static void scRD7() {
  // Kieses, reset, gyogyulas, ujabb kieses: a lepcso elolrol indul.
  ftIndit();
  auto r = ftJatszik(120, [](uint32_t p) {
    const bool rossz = (p < 20) || (p >= 60 && p < 80);
    return FtPerc{ !rossz, true, !rossz }; });
  ftKiir("ket kulon kieses", r);
  CHECK(r.size() == 2, "ket kieses: ket reset");
  CHECK(r.size() == 2 && r[1] >= 75.0 && r[1] < 78.0, "a masodik is 15 perc utan (nem 30 perces lepcso)");
}

static void scRD8() {
  // Halott router egy napig: nincs alvas, a resetek egyre ritkabbak.
  ftIndit();
  auto r = ftJatszik(24 * 60, [](uint32_t) { return FtPerc{ false, false, false }; });
  ftKiir("halott router 24 oran at", r);
  CHECK(r.size() == 9, "kilenc reset egy nap alatt");
  bool ritkul = true;
  for (size_t i = 1; i < r.size(); i++) if (r[i] - r[i - 1] < 30.0) ritkul = false;
  CHECK(ritkul, "ket reset kozott legalabb 30 perc");
  CHECK(r.size() >= 6 && r[5] - r[4] >= 240.0 && r[5] - r[4] < 241.5, "a lepcso 4 oranal megall");
  CHECK(deviceMode == (DeviceMode)0, "vegig figyel, nem alszik el");
}

static void scRD9() {
  // Heap-ujraindulas: a reset-szamlalo atjon, a kovetkezo reset a lepcso szerint jon.
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = 0;
  setup();
  testState.resetEvents = 2;
  g_freeHeap = 5000; g_maxAllocHeap = 4000;
  bool restarted = false; int guard = 0;
  try { while (!restarted && ++guard < 200000) { checkHeap(g_millis); g_millis += 1000; } }
  catch (RestartSignal&) { restarted = true; }
  CHECK(restarted, "a heap miatt ujraindult");
  const uint32_t hm = rtcHeapMagic, hr = rtcHeapRestarts, cr = rtcCarryRetryRounds;
  const uint8_t ce = rtcCarryResetEvents;
  coldBoot(true, "TestNet", "pw", "", "");
  rtcHeapMagic = hm; rtcHeapRestarts = hr; rtcCarryResetEvents = ce; rtcCarryRetryRounds = cr;
  setup();
  CHECK(testState.resetEvents == 2 && testState.outage, "a szamlalo es a kieses atjott");
  monitorUzembe();
  g_log.clear(); g_httpFailMs = 10000;
  auto r = ftJatszik(90, [](uint32_t) { return FtPerc{ false, false, false }; });
  ftKiir("heap-ujraindulas utan", r);
  CHECK(r.size() == 1 && r[0] >= 50.0, "a kovetkezo reset az 1 oras lepcso szerint jon");
}

static void scRD10() {
  // Wi-Fi van, de a DHCP nem adott gateway-t (0.0.0.0): nem pingel vakon, router hiba.
  ftIndit();
  wifiSim.dhcpGw = IPAddress();
  pingSim.targets.clear();
  CHECK(!gatewayPingOk(), "gateway nelkul: a router nem valaszol");
  CHECK(pingSim.targets.empty(), "0.0.0.0-t nem pingel");
}

static void scRD11() {
  // Wi-Fi vesztes egy kiesesen belul: a lepcso a Wi-Fi agra is vonatkozik.
  ftIndit();
  auto r = ftJatszik(5, [](uint32_t) { return FtPerc{ false, false, false }; });
  CHECK(r.size() == 1, "router hiba: egy reset");
  futtat(15u * 60 * 1000, [] { return currentState == (State)0 && serialHas("RESET_DELAY"); });
  const uint32_t elsoReset = testState.lastResetAt;
  wifiSim.willConnect = false; wifiSim.begun = false;
  g_log.clear();
  futtat(40u * 60 * 1000, [] { return relayPulses() > 0; });
  const uint32_t kozte = testState.lastResetAt - elsoReset;
  printf("     [info] a ket reset kozott %.1f perc\n", kozte / 60000.0);
  CHECK(serialHas("a kovetkezo router reset meg nem esedekes"), "a Wi-Fi vesztesre sem resetel azonnal");
  CHECK(relayPulses() == 1 && kozte >= 30u * 60 * 1000, "a masodik reset a 30 perces lepcso utan jon");
}

static void scRD12() {
  // Router hiba, reset - de utana rossz jelszo miatt nem sikerul visszacsatlakozni: AP mod.
  ftIndit();
  pingSim.perTarget["192.168.1.1"] = false; pingSim.perTarget["1.1.1.1"] = false; pingSim.perTarget["8.8.8.8"] = false;
  g_httpCode = -11;
  futtat(30u * 60 * 1000, [] { return relayPulses() >= 1 && serialHas("Router reset kesz"); });
  CHECK(relayPulses() == 1, "router hiba: reset");
  wifiSim.authFail = true; wifiSim.willConnect = false; wifiSim.begun = false;
  futtat(30u * 60 * 1000, [] { return deviceMode != (DeviceMode)0; });
  CHECK(deviceMode == (DeviceMode)1, "a reset utan rossz jelszo: AP beallito mod");
  CHECK(relayPulses() == 1, "es nem indit ujabb resetet");
}

static void scRD13() {
  // Az 1.1.1.1 nem valaszol, a 8.8.8.8 igen: van internet, HTTP sem kell.
  ftIndit();
  g_httpUrls.clear();
  std::vector<double> r;
  {
    const uint32_t t0 = g_millis; int guard = 0; size_t nezett = g_log.size();
    try {
      while (g_millis - t0 < 40u * 60 * 1000 && ++guard < 3000000) {
        g_httpCode = -11;
        pingSim.perTarget["192.168.1.1"] = false;
        pingSim.perTarget["1.1.1.1"] = false;
        pingSim.perTarget["8.8.8.8"] = true;
        loop();
        for (size_t i = nezett; i < g_log.size(); i++) if (g_log[i] == RELAY_HIGH) r.push_back(0);
        nezett = g_log.size();
      }
    } catch (DeepSleepSignal&) {}
  }
  CHECK(r.empty(), "a tartalek celpont valaszol: nincs reset");
  CHECK(evCount(EVC_TEST_FAIL) == 0, "kiesest sem naplozott");
  CHECK(g_httpUrls.empty(), "HTTP kerest sem kuldott");
  CHECK(std::count(pingSim.targets.begin(), pingSim.targets.end(), std::string("8.8.8.8")) > 0,
        "a 8.8.8.8-at tenyleg pingelte");
}

static void scRD14() {
  // Normal uzemben percenkent egy ellenorzes, a kor kezdetetol merve.
  ftIndit();
  g_serialLog.clear();
  ftJatszik(60, [](uint32_t) { return FtPerc{ true, true, true }; });
  int n = 0;
  for (auto& l : g_serialLog) if (l.find("Internet ellenorzes.") != std::string::npos) n++;
  printf("     [info] 60 perc alatt %d ellenorzes\n", n);
  CHECK(n >= 59 && n <= 61, "percenkent egy");
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
  { "E1: teljes egészséges ciklus (ellenőrzés -> várakozás -> ellenőrzés)", scE1 },
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
  { "SER8: a kuszob korul ingadozo heap sem araszt el", scSER8 },
  { "SER9: mindig buko naplomentes sem araszt el", scSER9 },
  { "SER10: a 33 kores ketnapos letra soros terhelese", scSER10 },
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
  { "H8: a tartalék HTTP teszt sorban váltja az öt végpontot", scH8 },
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
  { "WD11: a watchdog felélesztésének sorrendje és beállítása", scWD11 },
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
  { "R7: a felismerési idő router hibánál 150 mp élő és 168 mp halott DNS mellett", scR7 },
  { "R8: 33 kör, 32 alvás - a tényleges türelem ~46 óra", scR8 },
  { "CH1: a chunked válasz keretbájtjai nem buktatják el a tesztet", scCH1 },
  { "CH2: több darabra vágott választ is összefűz", scCH2 },
  { "CH3: darab-kiterjesztés és nagybetűs hexa", scCH3 },
  { "CH4: szabálytalan keretezés -> bukás, nem találgatás", scCH4 },
  { "CH5: chunked captive portal sem olvastat végig 50 kB-ot", scCH5 },

  { "OP1: a firstStartDelay korán véget ér, ha a hálózat ÉS az internet visszajött", scOP1 },
  { "OP2: ha csak a hálózat jön vissza, a delay szabályosan végigfut", scOP2 },
  { "OP3: ép induláskor az első próba azonnal fut (nem késik egy ütemet)", scOP3 },
  { "OP4: flash kímélés – nincs fájlírás, és percenként max egy WiFi.begin()", scOP4 },
  { "OP5: a RESET_DELAY korán zárul, és nem bontja le az igazolt kapcsolatot", scOP5 },
  { "OP6: a próba órái túlélik a millis() körbefordulását", scOP6 },
  { "OP7: élő Wi-Fi mellett is korán zárul, ha az internet menet közben visszajön", scOP7 },

  { "WDT8b: a LittleFS formázása is belefér a watchdog ablakába", scWDT8b },
  { "WDT9: 1 óra hibátlan működés AP módban is nullázza a WDT számlálót", scWDT9 },
  { "RR3: egyetlen sikeres ellenőrzés nullázza a reset-számlálót", scRR3 },
  { "LOG4: pislákoló kapcsolat nem árasztja el az eseménynaplót", scLOG4 },
  { "LOG5: a naplózás nem blokkol és nem függ a fájlrendszertől", scLOG5 },
  { "LOG1: a /log emberi olvasásra készül (reset ok, uptime, jelmagyarázat)", scLOG1 },
  { "LOG2: a naplóoldalon semmilyen konfigurációs érték nem jelenik meg", scLOG2 },
  { "LOG3: a körpuffer körbefordulása után is pontosan 32 sor", scLOG3 },
  { "LOG6: a naplo szamlaloja korbefordulhat - az index attol ep marad", scLOG6 },
  { "SH1: leallas elott a zarat MEG IS SZEREZZUK, nem csak megvarjuk", scSH1 },
  { "WDT14: a watchdog szamlalo a MOSTANI indulashoz kepest mer", scWDT14 },
  { "HP1: a heap allapotsora ritkitva megy ki, nem koronkent", scHP1 },
  { "NV1: a naplo a harom fontos pillanatban kimegy a fajlrendszerre", scNV1 },
  { "NV2: az iras sikeresseget visszaolvasassal ellenorizzuk", scNV2 },
  { "NV3: a mentes atomikusan szerzi meg a zarat, es fel is oldja", scNV3 },
  { "NV4: mentes kozben nincs gombos ujraindulas es masik iras", scNV4 },
  { "NV5: a lap a FRISSEBB naplot tolti be", scNV5 },
  { "NV6: hianyzo, ures vagy hibas fajl nem okoz gondot", scNV6 },
  { "NV7: NTP idobelyeg a bejegyzeseken", scNV7 },
  { "NV8: feluton buko betoltes nem hagy kevert puffert", scNV8 },
  { "NV9: mind a NEGY alvas menti a naplot, nem csak az idozitett ketto", scNV9 },
  { "NV10: az oraszinkron MINDEN kapcsolati uton elindul", scNV10 },
  { "NV11: sikertelen NTP kommunikacio nem okoz gondot", scNV11 },
  { "NV12: megtelt fajlrendszer a naplo mentese kozben", scNV12 },
  { "NV13: valtozas nelkul nem irunk ujra (flash kimeles)", scNV13 },
  { "NV14: a lap kiirja, MIKOR mentettuk a naplot", scNV14 },
  { "NV15: mind a 15 nevesitett indulasi ok emberi nevet kap", scNV15 },
  { "NV16: olvashatatlan fajlrendszer a naplo korul", scNV16 },
  { "AP6: mentes csatolatlan fajlrendszerrel -> tiszta 500", scAP6 },
  { "WF11: a router reset utan sem jon vissza a WiFi", scWF11 },
  { "AP5: aposztrof es tarsai az SSID-ben - HTML escape", scAP5 },
  { "AP7: mind a negy mezo egyformán turi a beillesztett szokozoket", scAP7 },
  { "AP8: a beallito urlap befer a stream kezdo pufferebe", scAP8 },
  { "LOG7: a /log oldal a legrosszabb esetben is befer a pufferbe", scLOG7 },
  { "LOG8: firmware frissites utan a regi elrendezesu naplo ervenytelen", scLOG8 },
  { "LOG9: a wifireset a naplofajlt szandekosan NEM torli", scLOG9 },
  { "HP2: a figyelmeztetes csak az atlepeskor szol", scHP2 },
  { "HP3: egyetlen melypont nem indit ujra, a tartos igen", scHP3 },
  { "HP4: a router reset szamlalo TULELI a heap-ujraindulast", scHP4 },
  { "HP5: AP modban, rele-impulzus es fajliras kozben nem indul ujra", scHP5 },
  { "HP6: harom sikertelen kor utan vegzetes hiba, nem boot loop", scHP6 },
  { "HP7: egy ora hibatlan mukodes nullazza a heap szamlalot", scHP7 },
  { "HP8: a ket uj esemenykod a /log oldalon is ertelmezheto", scHP8 },
  { "HP9: valodi lassu szivargas vegponttol vegpontig", scHP9 },
  { "HP10: a LOW HEAP bejegyzes sem araszthatja el a naplot", scHP10 },
  { "GWH1: a router reset utani ellenorzo ablakban nincs heap-ujraindulas", scGWH1 },
  { "GWH2: es az ablak utan a gateway-dontes rendesen megszuletik", scGWH2 },
  { "GWH3: mi marad meg es mi szamolodik ujra egy heap-ujraindulas utan", scGWH3 },
  { "GWH4: a 2 napos ablak szamlaloja is atmegy a heap-ujraindulason", scGWH4 },
  { "GWH5: gombnyomasnal viszont tovabbra is tiszta lap", scGWH5 },
  { "WDT15: ...es a millis() korbefordulasan at is", scWDT15 },
  { "CC1: a portal futasa alatt a loop nem olvassa a konfig puffereket", scCC1 },
  { "CC2: a vegzetes hiba again sem olvassa oket", scCC2 },
  { "CC3: a webszerver kezeloi egymassal sem versenyeznek", scCC3 },
  { "PWR1: aramszunet - a router bootolasa alatt nem nyul a relehez", scPWR1 },
  { "PWR2: aramszunet hosszu router-boottal - hol a turelem hatara", scPWR2 },
  { "PWR3: kezi aramtalanitas uzem kozben - mennyi a turelmi ido", scPWR3 },
  { "PWR4: idoben visszadugott router - az ESP nem nyul a relehez", scPWR4 },
  { "SH2: az alvasi utak elofeltetelei - idozito es gombebresztes", scSH2 },
  { "SH3: a felebreszto gombnyomast nem nezzuk beragadt gombnak", scSH3 },
  { "AP1: üres címmező törlésként értelmeződik (a DHCP-re váltás útja)", scAP1 },
  { "AP2: az előkitöltés SOHA nem tartalmazza a jelszót", scAP2 },
  { "AP3: az SSID HTML-escape-elve kerül a lapra (XSS ellen)", scAP3 },
  { "AP4: az előkitöltéssel a statikus IP megmarad jelszócserénél", scAP4 },
  { "FS11: sorvégek és whitespace a konfigfájlban", scFS11 },
  { "FS12: bináris szemét és túl hosszú fájl - nincs túlcsordulás", scFS12 },
  { "FS13: menet közben megtelő fájlrendszer - a zár felszabadul", scFS13 },
  { "FS14: félbeszakadt mentés - a régi érték is odavész (rögzített viselkedés)", scFS14 },
  { "LAT1: blokkoló szakaszba eső rövid gombnyomás sem vész el", scLAT1 },
  { "LAT2: a retesz NEM kerüli meg a debounce-t (zajtüske nem indít újra)", scLAT2 },
  { "LAT3: a wifireset gomb reteszelése is működik", scLAT3 },
  { "LAT4: fájlírás alatt a retesz megmarad – a nyomás késik, nem vész el", scLAT4 },
  { "LAT5: beragadt gomb nem reteszel (nincs felfutó él)", scLAT5 },
  { "LAT6: foglalt zárnál a retesz megmarad – a nyomás nem vész el", scLAT6 },
  { "LAT7: alvás előtt a gomb-megszakítások leválasztva", scLAT7 },
  { "BTN4: a reset gomb is atomikusan szerzi meg a konfigzárat", scBTN4 },
  { "SE11: az onlineProbe() WiFi.begin()-je is a NYÍLT jelszót adja", scSE11 },
  { "WF10: a RESET_DELAY korai kilépése után a statikus IP érintetlen", scWF10 },
  { "CFG2: sérült kódolt jelszó -> nem végzetes hiba, AP módban javítható", scCFG2 },
  { "LED4: a Wi-Fi LED legfeljebb egy percig hazudik", scLED4 },
  { "LED5: a státusz LED normál üzemben végig világít", scLED5 },
  { "LED6: AP módban a mentés utáni türelmi idő alatt is villog", scLED6 },
  { "BTN1: a hosszú várakozások alatt 10 ms-onként nézzük mindkét gombot", scBTN1 },
  { "BTN2: a blokkoló HTTP kérés alatti vak ablak mérése (egy kérésnyi)", scBTN2 },
  { "BTN3: a végig nyomva tartott gomb a vak ablak után is hat", scBTN3 },

  { "LED2: AP módban a Wi-Fi LED villog, a státusz LED végig világít", scLEDap },
  { "LED3: a reset pulzus villogása 2 Hz, és pontosan a pulzus alatt tart", scLEDpulse },

  { "WDT7: a watchdog már a LittleFS csatolása ELŐTT élesedik", scWDT_EARLY },
  { "SER4: kiesésnél az eltelt és a hátralévő idő percben", scSERidx },
  { "SER5: sikernél csak 'Successful Test', eltérésnél a kapott törzs is", scSERquiet },
  { "SER6: a soros port helyesen indul es rendezetten zar le alvas elott", scSER6 },
  { "SER7: a beragadt gomb agan sem veszik el az utolso sor", scSER7 },
  { "BNC1: pattogo gombnyomas - egyszer reteszel, nem hagy allapotot", scBNC1 },
  { "BNC2: pattogo tuske nem kerulheti meg a debounce-t", scBNC2 },
  { "BNC3: folyamatosan recsego (kopott) gomb nem indit ujra", scBNC3 },
  { "BNC4: a beragadt-gomb ellenorzes pattogasturo", scBNC4 },
  { "EVT1: a /log TEST FAIL paramétere is 1-alapú", scEVT1 },
  { "SYNC1: a hatarido+jelzo par EGY kritikus szakaszban", scSYNC1 },
  { "SYNC2: az ujraindulasi hatarido szemantikaja", scSYNC2 },
  { "SYNC3: a konfigzar megszerzese es feloldasa szimmetrikus", scSYNC3 },

  { "PROP1: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 1)", scPROP1 },
  { "PROP2: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 2)", scPROP2 },
  { "PROP3: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 3)", scPROP3 },
  { "PROP4: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 4)", scPROP4 },
  { "PROP5: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 5)", scPROP5 },
  { "PROP6: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 6)", scPROP6 },
  { "PROP7: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 7)", scPROP7 },
  { "PROP8: veletlen kornyezeti esemenysorozat - tulajdonsagok (mag 8)", scPROP8 },

  { "ILV1: /ping a loop() minden delay()-eben (a legsurubb osszefonas)", scILV1 },
  { "ILV2: /log olvasas a naplo irasa kozben", scILV2 },
  { "ILV3: POST mentes a loop() koze ekelve - zar es halasztott ujrainditas", scILV3 },
  { "ILV4: a loop() a flash-iras KOZBEN - a kilepesi ut nem kerulheti meg a zarat", scILV4 },
  { "ILV5: mindket irany egyszerre, veletlen suruseggel", scILV5 },

  { "WEAR: mukodo internet mellett nulla flash-iras", scWEAR },
  { "RST1: heap miatti ujraindulas nem veszit naplobejegyzest", scRST1 },
  { "RST2: watchdog reset nem veszit naplobejegyzest", scRST2 },
  { "RST3: kritikus heapnel a sikertelen mentes sem art", scRST3 },
  { "GAP: elveszett fajl mellett az RTC teljes tartalma latszik", scGAP },
  { "GAP2: aramszunet utan sem elrejtes, sem ketszerezes", scGAP2 },
  { "COV7: a naplofajl olvasasi hibaagai", scCOV7 },
  { "COV8: a soros olvasas vedelmi aga", scCOV8 },
  { "CMD1: a soros LOG parancs mindket naplot kiirja", scCMD1 },
  { "CMD2: a parancs-feldolgozas nem blokkol es nem ragad be", scCMD2 },
  { "CMD3: a LOG parancs MODE_FATAL-ban is mukodik", scCMD3 },
  { "NV20: aramszunet utan a naplofajl tortenete megmarad", scNV20 },
  { "NV21: a naplofajl gyuruje a legregebbieket ejti", scNV21 },
  { "NV22: uj esemeny nelkul nincs iras (a flash kopasa)", scNV22 },
  { "COV1: eventName() ismeretlen kodra", scCOV1 },
  { "COV2: a naplo-betolto sajat hatarellenorzese (a hivotol fuggetlenul)", scCOV2 },
  { "COV3: a naplofajl eltunik a fejlec es a bejegyzesek olvasasa kozott", scCOV3 },
  { "COV4: konyvtar egy konfig utvonalon", scCOV4 },
  { "COV5: a wifireset foglalt zarnal nem torol es nem indit ujra", scCOV5 },
  { "COV6: a statikus gateway-ellenorzes (WiFi.gatewayIP alapjan)", scCOV6 },

  { "SERL: a soros /log parancs is mukodik", scSERlog },
  { "HC1: a HTTP kod naplozhato alakja es rogzitese", scHC1 },
  { "HC2: az elso bukas a HTTP kodot is naplozza", scHC2 },
  { "FB1: tartalek DNS DHCP mellett is", scFB1 },
  { "SL1: a Wi-Fi modem-alvas kikapcsolva", scSL1 },
  { "WL1: Wi-Fi vesztes rossz jelszoval - AP mod, nem router reset", scWL1 },
  { "WL2: Wi-Fi vesztes jelszohiba nelkul - marad a router reset", scWL2 },
  { "SP1: a statikus IP par ellenorzese", scSP1 },
  { "SP2: osszeillo par nelkuli regi konfig - DHCP", scSP2 },
  { "SP3: a portal nem ment osszeillo par nelkuli statikus IP-t", scSP3 },
  { "PW1: a jelszo szokozei megmaradnak, a sorveg nem", scPW1 },
  { "PG3: az ESPping fantom-sikere bukasnak szamit", scPG2 },
  { "RD1: router hiba (a router sem valaszol): reset kb. 2 perc utan", scRD1 },
  { "RD2: 5-14 perces szolgaltatoi kieses: nincs reset", scRD2 },
  { "RD3: 6 oras szolgaltatoi kieses: 15 perc, majd 30 perc / 1 ora / 2 ora lepcso", scRD3 },
  { "RD4: a 2026-11-22-i terepi nap: nulla reset", scRD4 },
  { "RD5: pingre nem valaszolo router: router hibanak latszik", scRD5 },
  { "RD6: szurt ping, mukodo HTTP: nincs reset, az ot vegpont sorra kerul", scRD6 },
  { "RD7: gyogyulas utan a lepcso elolrol indul", scRD7 },
  { "RD8: halott router egy napig: nincs alvas, legfeljebb 4 orankent reset", scRD8 },
  { "RD9: heap-ujraindulas: a reset-szamlalo es a lepcso atjon", scRD9 },
  { "RD10: Wi-Fi van, gateway nincs: router hiba, vak ping nelkul", scRD10 },
  { "RD11: Wi-Fi vesztes a kiesesen belul: a lepcso itt is ervenyes", scRD11 },
  { "RD12: reset utan rossz jelszo: AP mod", scRD12 },
  { "RD13: az 1.1.1.1 hallgat, a 8.8.8.8 valaszol: nincs reset", scRD13 },
  { "RD14: normal uzemben percenkent egy ellenorzes", scRD14 },

  { "APP1: az elso portal-inditas egyedi, eros AP jelszot general", scAPP1 },
  { "APP2: az AP jelszo allando, indulaskor kiirva, normal uzemben nincs iras", scAPP2 },
  { "APP3: a wifireset nem torli az AP jelszot", scAPP3 },
  { "APP4: ket eszkoz - ket jelszo, ket AP nev", scAPP4 },
  { "APP5: serult mentett AP jelszo helyett uj", scAPP5 },
  { "APP6: nem irhato fajlrendszer mellett is indul a portal", scAPP6 },
};

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

    if (getenv("WDTGAP")) fprintf(stderr, "WDTGAP %s | %u\n", sc.name, (unsigned)g_wdtMaxFeedGap);

    if (getenv("PROBE")) fprintf(stderr, "PROBE|%s|crit=%d|ser=%zu|ev=%u|ms=%u\n",
        sc.name, g_criticalMaxDepth, g_serialLog.size(), (unsigned)rtcEvNext,
        (unsigned)g_millis);

    CHECK(g_criticalMaxDepth <= 1,
          "a kritikus szakasz sosem agyazodott egymasba");
    if (g_wdtGapWaiver != nullptr) {
      printf("     [info] a watchdog-res invarians alol felmentve: %s\n", g_wdtGapWaiver);
    } else {
      CHECK(g_wdtMaxFeedGap <= WDT_FEED_GAP_LIMIT_MS,
            "a leghosszabb etetes nelkuli szakasz a kuszob alatt maradt");
    }
    Result r{checks, failures};
    ssize_t n = write(fds[1], &r, sizeof(r));
    (void)n;
    close(fds[1]);
    fflush(stdout);
#ifdef COVERAGE_BUILD

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
