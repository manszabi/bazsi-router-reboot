#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPping.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <ESPAsyncWebServer.h>

uint32_t g_millis = 0;
std::vector<std::string> g_log;
std::vector<std::string> g_serialLog;
std::map<int,int> g_pinState;
std::map<int,int> g_pinRead;
bool g_serialOn = false;
std::map<std::string,std::string> g_fs;
bool g_fsMountOk = true;
bool   g_fsWritable = true;
size_t g_fsCapacity = 0;
bool   g_fsRemoveOk = true;
bool   g_fsReadable = true;
esp_reset_reason_t g_resetReason = ESP_RST_POWERON;
esp_reset_reason_t esp_reset_reason(void) { return g_resetReason; }
size_t g_fsUsed() { size_t n = 0; for (auto& kv : g_fs) n += kv.second.size(); return n; }
uint64_t g_wakeupUs = 0;
uint64_t g_gpioWakeMask = 0;
int g_gpioWakeMode = -1;
bool g_serialEcho = false;
int g_httpCode = 200;
std::string g_httpBody = "Microsoft NCSI";
int g_httpSize = -2;
bool g_httpBeginOk = true;
bool     g_wdtEnabled = false;
uint32_t g_wdtTimeoutMs = 0;
uint32_t g_wdtIdleMask = 0xFFFFFFFF;
bool     g_wdtPanic = false;
uint32_t g_wdtMaxFeedGap = 0;
uint32_t g_wdtLastFeed = 0;
uint32_t g_wdtFeedBeforeEnable = 0;
bool     g_wdtTrack = false;
static bool g_wdtInited = true;   // IDF: ESP_TASK_WDT_INIT=y -> boot óta fut

static void wdtApply(const esp_task_wdt_config_t* c) {
  g_wdtTimeoutMs = c->timeout_ms; g_wdtIdleMask = c->idle_core_mask; g_wdtPanic = c->trigger_panic;
}
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t* c) {
  if (g_wdtInited) return ESP_ERR_INVALID_STATE;
  g_wdtInited = true; wdtApply(c); simLog("wdt_init"); return ESP_OK;
}
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* c) {
  if (!g_wdtInited) return ESP_ERR_INVALID_STATE;
  wdtApply(c); simLog("wdt_reconfigure"); return ESP_OK;
}
void enableLoopWDT() { g_wdtEnabled = true; simLog("enableLoopWDT"); }
void feedLoopWDT() {
  if (!g_wdtEnabled) g_wdtFeedBeforeEnable++;
  if (g_wdtTrack) {
    const uint32_t gap = g_millis - g_wdtLastFeed;
    if (gap > g_wdtMaxFeedGap) g_wdtMaxFeedGap = gap;
  }
  g_wdtLastFeed = g_millis;
  simLog("wdt_feed");
}
void delay(uint32_t ms) { g_millis += ms ? ms : 1; }

HardwareSerial Serial;
EspClass ESP;
WiFiClass WiFi;
LittleFSClass LittleFS;
std::map<std::string, ArRequestHandlerFunction> g_handlers;
PingClass Ping;
WifiSim wifiSim;
PingSim pingSim;

void simLog(const std::string& s) { g_log.push_back(s); }
void Print::flushLine() {
  if (g_serialEcho) printf("    | %s\n", buf_.c_str());
  if (g_serialLog.size() < 5000) g_serialLog.push_back(buf_);
  buf_.clear();
}

unsigned long millis() { return g_millis; }
void pinMode(uint8_t p, uint8_t m) { (void)m; g_pinState[p] = -1; }
void digitalWrite(uint8_t p, uint8_t v) {
  if (g_pinState[p] != v) simLog("pin" + std::to_string(p) + "=" + (v ? "HIGH" : "LOW"));
  g_pinState[p] = v;
}
int digitalRead(uint8_t p) { auto it = g_pinRead.find(p); return it == g_pinRead.end() ? HIGH : it->second; }
void yield() { g_millis += 10; }   // szimulált idő telik minden yield()-nél
int64_t esp_timer_get_time() { return (int64_t)g_millis * 1000; }
void esp_sleep_enable_timer_wakeup(uint64_t us) { g_wakeupUs = us; simLog("timer_wakeup(" + std::to_string(us) + ")"); }
int g_gpioWakeResult = 0;
static int armGpioWake(uint64_t mask, int mode) {
  if (g_gpioWakeResult != 0) return g_gpioWakeResult;   // armolas elbukott
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
size_t strlcpy(char* d, const char* s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; }
  return l;
}
