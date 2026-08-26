#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
// A valódi XIAO_ESP32C3 variants/pins_arduino.h szerint
#define D0 2
#define D1 3
#define D3 5
#define D4 6
#define D5 7

// --- szimulált idő / naplózás ---
extern uint32_t g_millis;
extern std::vector<std::string> g_log;
extern std::map<int,int> g_pinState;
extern std::map<int,int> g_pinRead;
extern bool g_serialOn;
void simLog(const std::string& s);

struct RestartSignal { };
struct DeepSleepSignal { uint64_t us; };

unsigned long millis();
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int digitalRead(uint8_t);
void yield();
void delay(uint32_t ms);
void feedLoopWDT();
void enableLoopWDT();
extern bool     g_wdtEnabled;
extern uint32_t g_wdtTimeoutMs;
extern uint32_t g_wdtIdleMask;
extern bool     g_wdtPanic;
extern uint32_t g_wdtMaxFeedGap;   // leghosszabb szakasz etetés nélkül
extern uint32_t g_wdtLastFeed;
extern bool     g_wdtTrack;
int64_t esp_timer_get_time();
void esp_sleep_enable_timer_wakeup(uint64_t);
void esp_deep_sleep_start();
void esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(uint64_t, int);
#define ESP_GPIO_WAKEUP_GPIO_LOW 0
#define BIT(n) (1ULL << (n))
extern uint64_t g_wakeupUs;
extern uint64_t g_gpioWakeMask;
extern int g_gpioWakeMode;
extern bool g_serialEcho;
size_t strlcpy(char*, const char*, size_t);

class String {
public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
  String(unsigned v) { char b[24]; snprintf(b, sizeof(b), "%u", v); s_ = b; }
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool equals(const char* o) const { return s_ == o; }
  void trim() {}
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  bool operator==(const char* o) const { return s_ == o; }
private:
  std::string s_;
};

class Stream {
public:
  virtual ~Stream() {}
  virtual int read() { return -1; }
  virtual int available() { return 0; }
  size_t readBytes(char*, size_t) { return 0; }
  size_t readBytesUntil(char, char*, size_t) { return 0; }
  void setTimeout(unsigned long) {}
};

class IPAddress;

class Print {
public:
  size_t print(const IPAddress&);
  size_t println(const IPAddress&);
  size_t print(const char* s) { buf_ += s; return 0; }
  size_t print(const String& s) { buf_ += s.c_str(); return 0; }
  size_t print(int v) { buf_ += std::to_string(v); return 0; }
  size_t print(unsigned v) { buf_ += std::to_string(v); return 0; }
  size_t print(unsigned long v) { buf_ += std::to_string(v); return 0; }
  size_t print(long v) { buf_ += std::to_string(v); return 0; }
  size_t println(const char* s) { buf_ += s; flushLine(); return 0; }
  size_t println(const String& s) { buf_ += s.c_str(); flushLine(); return 0; }
  size_t println(int v) { buf_ += std::to_string(v); flushLine(); return 0; }
  size_t println(unsigned v) { buf_ += std::to_string(v); flushLine(); return 0; }
  size_t println(unsigned long v) { buf_ += std::to_string(v); flushLine(); return 0; }
  size_t println(long v) { buf_ += std::to_string(v); flushLine(); return 0; }
  size_t println() { flushLine(); return 0; }
  size_t printf(const char* f, ...) { (void)f; return 0; }
protected:
  void flushLine();
  std::string buf_;
};

class HardwareSerial : public Print {
public:
  void begin(unsigned long) { g_serialOn = true; }
  void end() { g_serialOn = false; }
  void flush() {}
  operator bool() const { return true; }
};
extern HardwareSerial Serial;

class IPAddress : public Print {
public:
  IPAddress() { o[0]=o[1]=o[2]=o[3]=0; }
  IPAddress(uint32_t) { o[0]=o[1]=o[2]=o[3]=0; }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { o[0]=a;o[1]=b;o[2]=c;o[3]=d; }
  bool fromString(const char* s) {
    unsigned a,b,c,d; char extra;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a,&b,&c,&d,&extra) != 4) return false;
    if (a>255||b>255||c>255||d>255) return false;
    o[0]=(uint8_t)a;o[1]=(uint8_t)b;o[2]=(uint8_t)c;o[3]=(uint8_t)d; return true;
  }
  std::string str() const {
    char t[20]; snprintf(t,sizeof(t),"%u.%u.%u.%u",o[0],o[1],o[2],o[3]); return t;
  }
  bool isZero() const { return !o[0] && !o[1] && !o[2] && !o[3]; }
  uint8_t o[4];
};

inline size_t Print::print(const IPAddress& a) { buf_ += a.str(); return 0; }
inline size_t Print::println(const IPAddress& a) { buf_ += a.str(); flushLine(); return 0; }

class EspClass {
public:
  void restart() { simLog("ESP.restart"); throw RestartSignal{}; }
  const char* getChipModel() { return "ESP32-C3"; }
};
extern EspClass ESP;
