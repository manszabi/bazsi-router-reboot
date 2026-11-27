// A stubok közös állapota és a nem inline modellek.
#include <Arduino.h>
#include <set>
#include <cstdarg>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPping.h>
#include <WiFiUdp.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <ESPAsyncWebServer.h>
#include <driver/gpio.h>

uint32_t g_millis = 0;
std::vector<std::string> g_log;
std::vector<std::string> g_serialLog;
std::map<int,int> g_pinState;
std::map<int,int> g_pinRead;
bool g_serialOn = false;
unsigned long g_serialBaud = 0;
uint32_t g_serialFirstWriteMs = 0;
int      g_serialWritesAfterEnd = 0;
bool     g_serialFlushedAll = true;
void HardwareSerial::end() { g_serialOn = false; simLog("Serial.end"); }
void HardwareSerial::flush() { g_serialFlushedAll = true; simLog("Serial.flush"); }
std::map<std::string,std::string> g_fs;
bool g_fsMountOk = true;
uint32_t g_fsMountMs = 0;
bool   g_fsWritable = true;
size_t g_fsCapacity = 0;
bool   g_fsRemoveOk = true;
bool   g_fsReadable = true;
bool   g_fsSilentWriteFail = false;
bool   g_fsShortRead = false;
int  g_fsShortReadSkip = 0;
esp_reset_reason_t g_resetReason = ESP_RST_POWERON;
esp_reset_reason_t esp_reset_reason(void) { return g_resetReason; }
size_t g_fsUsed() { size_t n = 0; for (auto& kv : g_fs) n += kv.second.size(); return n; }
uint64_t g_wakeupUs = 0;
uint64_t g_gpioWakeMask = 0;
int g_gpioWakeMode = -1;
bool g_serialEcho = false;
uint64_t g_efuseMac = 0x0000A1B2C3D4E5F6ULL;
uint32_t g_freeHeap = 180000;
uint32_t g_minFreeHeap = 180000;
uint32_t g_maxAllocHeap = 110000;
uint32_t g_heapDrainPerCall = 0;
int      g_heapQueries = 0;
uint32_t EspClass::getFreeHeap() {
  g_heapQueries++;
  if (g_heapDrainPerCall) {
    g_freeHeap = (g_freeHeap > g_heapDrainPerCall) ? g_freeHeap - g_heapDrainPerCall : 0;
    if (g_freeHeap < g_minFreeHeap) g_minFreeHeap = g_freeHeap;

    if (g_maxAllocHeap > g_freeHeap) g_maxAllocHeap = g_freeHeap;
  }
  return g_freeHeap;
}
uint32_t EspClass::getMaxAllocHeap() { return g_maxAllocHeap; }
int g_httpCode = 200;
std::string g_httpBody = "Microsoft NCSI";
int g_httpSize = -2;
bool g_httpBeginOk = true;
bool g_httpStall = false;
std::vector<std::string> g_httpUrls;
bool g_httpChunked = false;
size_t g_httpChunkSize = 0;
std::string g_httpRawOverride;
bool g_httpAutoOk = false;
uint32_t g_httpOkMs = 200;
uint32_t g_httpFailMs = 15000;
bool     g_wdtEnabled = false;
uint32_t g_wdtTimeoutMs = 0;
uint32_t g_wdtIdleMask = 0xFFFFFFFF;
bool     g_wdtPanic = false;
uint32_t g_wdtMaxFeedGap = 0;
uint32_t g_wdtLastFeed = 0;
uint32_t g_wdtFeedBeforeEnable = 0;
bool     g_wdtTrack = false;
bool g_wdtInited = true;
bool g_wdtInitFails = false;
bool g_wdtReconfigureFails = false;
uint32_t g_wdtFeedNotSubscribed = 0;

static void wdtApply(const esp_task_wdt_config_t* c) {
  g_wdtTimeoutMs = c->timeout_ms; g_wdtIdleMask = c->idle_core_mask; g_wdtPanic = c->trigger_panic;
}
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t* c) {
  if (g_wdtInited) return ESP_ERR_INVALID_STATE;
  if (g_wdtInitFails) { simLog("wdt_init_FAIL"); return ESP_FAIL; }

  g_wdtInited = true; wdtApply(c); simLog("wdt_init"); return ESP_OK;
}
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* c) {
  if (!g_wdtInited) return ESP_ERR_INVALID_STATE;
  if (g_wdtReconfigureFails) { simLog("wdt_reconfigure_FAIL"); return ESP_FAIL; }
  wdtApply(c); simLog("wdt_reconfigure"); return ESP_OK;
}

void enableLoopWDT() {
  if (!g_wdtInited) { simLog("enableLoopWDT_FAIL"); return; }
  g_wdtEnabled = true; simLog("enableLoopWDT");
}

void disableLoopWDT() {
  if (g_wdtEnabled) { g_wdtEnabled = false; simLog("disableLoopWDT"); }
}
esp_err_t esp_task_wdt_status(void*) {
  if (!g_wdtInited) return ESP_ERR_INVALID_STATE;
  return g_wdtEnabled ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void relayTrackTime();
static uint32_t g_wdtUnfedMs = 0;
bool g_wdtInProgram = false;

void wdtAdvanceTime(uint32_t ms) {
  relayTrackTime();
  if (!g_wdtEnabled || !g_wdtInProgram) {
    g_wdtUnfedMs = 0;
    return;
  }
  g_wdtUnfedMs += ms;
  if (g_wdtUnfedMs > g_wdtMaxFeedGap) g_wdtMaxFeedGap = g_wdtUnfedMs;
}

void wdtNoteTime() { wdtAdvanceTime(0); }

void feedLoopWDT() {
  if (!g_wdtEnabled) g_wdtFeedNotSubscribed++;
  if (!g_wdtEnabled) g_wdtFeedBeforeEnable++;
  g_wdtUnfedMs = 0;
  g_wdtLastFeed = g_millis;
  simLog("wdt_feed");
}

void feedLoopWDT_fromCore() {
  if (!g_wdtEnabled) return;
  g_wdtUnfedMs = 0;
  g_wdtLastFeed = g_millis;
}

void (*g_onDelay)() = nullptr;

void (*g_onFsWrite)() = nullptr;
std::set<std::string> g_fsDirs;
std::string g_serialIn;
bool g_serialReadLies = false;
void delay(uint32_t ms) { const uint32_t d = ms ? ms : 1; g_millis += d; wdtAdvanceTime(d); if (g_onDelay) g_onDelay(); }

HardwareSerial Serial;
EspClass ESP;
WiFiClass WiFi;
LittleFSClass LittleFS;
std::map<std::string, ArRequestHandlerFunction> g_handlers;
std::map<std::string, ArRequestHandlerFunction> g_statusHandlers;
int g_serverCount = 0;
bool g_statusServerRunning = false;
PingClass Ping;
WifiSim wifiSim;
PingSim pingSim;
DnsSim dnsSim;

void simLog(const std::string& s) { g_log.push_back(s); }

void Print::emit(const char* s) {
  for (const char* p = s; *p; p++) {
    if (*p == '\n') { flushLine(); }
    else if (*p != '\r') { buf_ += *p; }
  }
}

size_t Print::printf(const char* f, ...) {
  char b[512];
  va_list ap; va_start(ap, f);
  const int n = vsnprintf(b, sizeof(b), f, ap);
  va_end(ap);
  emit(b);
  return n < 0 ? 0 : (size_t)n;
}

void Print::flushLine() {
  if (g_serialFirstWriteMs == 0) g_serialFirstWriteMs = g_millis;
  if (!g_serialOn) g_serialWritesAfterEnd++;
  g_serialFlushedAll = false;

  if (g_serialEcho) ::printf("    | %s\n", buf_.c_str());
  if (g_serialLog.size() < 5000) g_serialLog.push_back(buf_);
  buf_.clear();
}

uint32_t millis() { return g_millis; }
void pinMode(uint8_t p, uint8_t m) { (void)m; g_pinState[p] = -1; }

uint32_t g_relayMaxHighMs = 0;
uint32_t g_relayHighSince = 0;
bool     g_relayIsHigh    = false;
static const int RELAY_PIN_STUB = 10;

void digitalWrite(uint8_t p, uint8_t v) {
  if (g_pinState[p] != v) simLog("pin" + std::to_string(p) + "=" + (v ? "HIGH" : "LOW"));
  if ((int)p == RELAY_PIN_STUB) {
    if (v && !g_relayIsHigh) {
      g_relayIsHigh = true; g_relayHighSince = g_millis;
    } else if (!v && g_relayIsHigh) {
      g_relayIsHigh = false;
      const uint32_t hold = g_millis - g_relayHighSince;
      if (hold > g_relayMaxHighMs) g_relayMaxHighMs = hold;
    }
  }
  g_pinState[p] = v;
}

void relayTrackTime() {
  if (g_relayIsHigh) {
    const uint32_t hold = g_millis - g_relayHighSince;
    if (hold > g_relayMaxHighMs) g_relayMaxHighMs = hold;
  }
}

std::map<int, IsrFn> g_isr;
std::map<int, int>   g_isrMode;
void attachInterrupt(uint8_t pin, IsrFn fn, int mode) {
  g_isr[pin] = fn; g_isrMode[pin] = mode;
  simLog("attachInterrupt(" + std::to_string(pin) + "," + std::to_string(mode) + ")");
}
void detachInterrupt(uint8_t pin) { g_isr.erase(pin); simLog("detachInterrupt(" + std::to_string(pin) + ")"); }
void simIsr(int pin) { auto it = g_isr.find(pin); if (it != g_isr.end() && it->second) it->second(); }

bool     g_btnTrack = false;
uint32_t g_btnLastPoll = 0;
uint32_t g_btnMaxGap = 0;
int digitalRead(uint8_t p) {
  if (g_btnTrack && (p == 2 || p == 3)) {
    const uint32_t gap = g_millis - g_btnLastPoll;
    if (gap > g_btnMaxGap) g_btnMaxGap = gap;
    g_btnLastPoll = g_millis;
  }
  auto it = g_pinRead.find(p); return it == g_pinRead.end() ? HIGH : it->second;
}
void yield() { g_millis += 10; wdtAdvanceTime(10); }
int64_t esp_timer_get_time() { return (int64_t)g_millis * 1000; }
void esp_sleep_enable_timer_wakeup(uint64_t us) { g_wakeupUs = us; simLog("timer_wakeup(" + std::to_string(us) + ")"); }
int g_gpioWakeResult = 0;
static int armGpioWake(uint64_t mask, int mode) {
  if (g_gpioWakeResult != 0) return g_gpioWakeResult;
  g_gpioWakeMask = mask; g_gpioWakeMode = mode;
  simLog("gpio_wakeup(mask=" + std::to_string(mask) + ",mode=" + std::to_string(mode) + ")");
  return 0;
}
int esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(uint64_t m, int mo) { return armGpioWake(m, mo); }
int esp_deep_sleep_enable_gpio_wakeup(uint64_t m, int mo) { return armGpioWake(m, mo); }
void esp_sleep_disable_wakeup_source(int) {
  g_wakeupUs = 0; g_gpioWakeMask = 0; g_gpioWakeMode = -1;
  simLog("wakeup_disable_all");
}
void esp_deep_sleep_start() { simLog("DEEP_SLEEP"); throw DeepSleepSignal{g_wakeupUs}; }

std::set<int> g_heldPins;
bool g_deepSleepHoldEnabled = false;
bool g_gpioHoldFails = false;
int gpio_hold_en(gpio_num_t p) {
  if (g_gpioHoldFails) { simLog("gpio_hold_en_FAIL"); return ESP_FAIL; }
  g_heldPins.insert(p);
  simLog("gpio_hold_en(" + std::to_string(p) + ")");
  return ESP_OK;
}
int gpio_hold_dis(gpio_num_t p) {
  g_heldPins.erase(p);
  simLog("gpio_hold_dis(" + std::to_string(p) + ")");
  return ESP_OK;
}
void gpio_deep_sleep_hold_en()  { g_deepSleepHoldEnabled = true;  simLog("deep_sleep_hold_en"); }
void gpio_deep_sleep_hold_dis() { g_deepSleepHoldEnabled = false; simLog("deep_sleep_hold_dis"); }

int g_criticalDepth = 0;
int g_criticalMaxDepth = 0;

int g_criticalEnters = 0;
void portENTER_CRITICAL(portMUX_TYPE*) {
  g_criticalEnters++;
  g_criticalDepth++;
  if (g_criticalDepth > g_criticalMaxDepth) g_criticalMaxDepth = g_criticalDepth;
}
void portEXIT_CRITICAL(portMUX_TYPE*) { g_criticalDepth--; }

#if !(defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38)))
size_t strlcpy(char* d, const char* s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; }
  return l;
}
#endif

uint32_t g_epochNow = 0;
uint32_t g_epochTickFrom = 0;
int      g_ntpStarts = 0;
void configTzTime(const char* tz, const char* server) {
  (void)tz; (void)server;
  g_ntpStarts++;
  simLog("configTzTime");
}

#undef time
time_t stub_time(time_t* out) {
  const time_t t = (time_t)(g_epochNow == 0 ? 0 : g_epochNow + (g_epochTickFrom != 0 ? (g_millis - g_epochTickFrom) / 1000 : 0));
  if (out) *out = t;
  return t;
}

int g_randomCalls = 0;
uint32_t g_randomState = 0x12345678u;
uint32_t esp_random(void) {
  g_randomCalls++;
  simLog("esp_random");
  g_randomState ^= g_randomState << 13;
  g_randomState ^= g_randomState >> 17;
  g_randomState ^= g_randomState << 5;
  return g_randomState;
}
