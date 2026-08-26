#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPping.h>

uint32_t g_millis = 0;
std::vector<std::string> g_log;
std::map<int,int> g_pinState;
std::map<int,int> g_pinRead;
bool g_serialOn = false;
std::map<std::string,std::string> g_fs;
bool g_fsMountOk = true;
uint64_t g_wakeupUs = 0;
uint64_t g_gpioWakeMask = 0;
int g_gpioWakeMode = -1;
bool g_serialEcho = false;
int g_httpCode = 200;
std::string g_httpBody = "Microsoft NCSI";
int g_httpSize = -2;
bool g_httpBeginOk = true;

HardwareSerial Serial;
EspClass ESP;
WiFiClass WiFi;
LittleFSClass LittleFS;
PingClass Ping;
WifiSim wifiSim;
PingSim pingSim;

void simLog(const std::string& s) { g_log.push_back(s); }
void Print::flushLine() {
  if (g_serialEcho) printf("    | %s\n", buf_.c_str());
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
void esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(uint64_t mask, int mode) {
  g_gpioWakeMask = mask; g_gpioWakeMode = mode;
  simLog("gpio_wakeup(mask=" + std::to_string(mask) + ",mode=" + std::to_string(mode) + ")");
}
void esp_deep_sleep_start() { simLog("DEEP_SLEEP"); throw DeepSleepSignal{g_wakeupUs}; }
size_t strlcpy(char* d, const char* s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; }
  return l;
}
