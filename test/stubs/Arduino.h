#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>  // strcasecmp - a String::equalsIgnoreCase mogott
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
extern std::vector<std::string> g_serialLog;  // a soros kimenet sorai
extern std::map<int,int> g_pinState;
extern std::map<int,int> g_pinRead;
extern bool g_serialOn;
void simLog(const std::string& s);

struct RestartSignal { };
struct DeepSleepSignal { uint64_t us; };

// FONTOS: az ESP32-C3 32 bites, ott az `unsigned long` = 32 bit, tehat a
// millis() uint32_t-t ad. A hoston viszont az `unsigned long` 64 BITES - ha a
// stub is azt adna vissza, a "millis() - start" korbefordulas-biztos idiomak
// MASKEPP viselkednenek, mint a celhardveren, es a wrap koruli viselkedes
// tesztelese ertelmetlen lenne. Ezert itt kifejezetten uint32_t.
uint32_t millis();
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int digitalRead(uint8_t);
void yield();
void delay(uint32_t ms);
// Minden delay()-nel meghivodik, ha be van allitva: ezzel modellezik a tesztek,
// hogy egy masik task kozben befejez valamit (lasd main_stub.cpp).
extern void (*g_onDelay)();
void feedLoopWDT();
void enableLoopWDT();
extern bool     g_wdtEnabled;
extern uint32_t g_wdtTimeoutMs;
extern uint32_t g_wdtIdleMask;
extern bool     g_wdtPanic;
extern uint32_t g_wdtMaxFeedGap;   // leghosszabb szakasz etetés nélkül
extern uint32_t g_wdtLastFeed;
extern uint32_t g_wdtFeedBeforeEnable;  // etetes a feliratkozas ELOTT
extern bool     g_wdtInited;            // fut-e mar a TWDT (ESP_TASK_WDT_INIT)
extern bool     g_wdtInitFails;         // az esp_task_wdt_init() hibat adjon
extern bool     g_wdtReconfigureFails;  // az esp_task_wdt_reconfigure() hibazzon
extern uint32_t g_wdtFeedNotSubscribed; // ennyi log_e() sor menne ki
extern bool     g_wdtTrack;
int64_t esp_timer_get_time();
void esp_sleep_enable_timer_wakeup(uint64_t);
void esp_deep_sleep_start();
void esp_sleep_disable_wakeup_source(int);
#define ESP_SLEEP_WAKEUP_ALL 0
// Mindket nev letezik, hogy a sketch mindket aga leforduljon.
int esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(uint64_t, int);
int esp_deep_sleep_enable_gpio_wakeup(uint64_t, int);
extern int g_gpioWakeResult;   // 0 = sikeres armolas, mas = hiba
#define ESP_GPIO_WAKEUP_GPIO_LOW 0
#define BIT(n) (1ULL << (n))
#define F(x) (x)
// A valódi RTC_DATA_ATTR deep sleepet túlélő memóriába tesz. A teszt egyetlen
// processzben fut, ezért itt sima globális - a persztenciát kézzel modellezzük.
#define RTC_DATA_ATTR
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
  // core: String::equalsIgnoreCase(const String&) - a fejlecek erteke tetszoleges
  // kis/nagybetus lehet, a HTTP fejlecek ertekei nem case-sensitive-ek.
  bool equalsIgnoreCase(const char* o) const {
    return strcasecmp(s_.c_str(), o ? o : "") == 0;
  }
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
  // A valodi Print::printf formaz es kiir. Ha ezt no-opkent hagyjuk, a
  // Serial.printf()-fel irt diagnosztika (konfig fajlok irasa/olvasasa/torlese,
  // POST parameterek) LATHATATLAN a tesztek szamara - vagyis ellenorizhetetlen.
  size_t printf(const char* f, ...);
protected:
  void flushLine();
  void emit(const char* s);
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

// A core IPAddress-e IPv6-ot is kezel, es ez itt szamit:
//  - fromString() eloszor IPv4-kent, majd IPv6-kent probal (IPAddress.cpp),
//    tehat a "::1" is ervenyes cimnek szamit;
//  - az uint32_t konverzio viszont IPv6-ra 0-t ad (IPAddress.h:83), es a
//    WiFi.config() pont ezt hasznalja (NetworkInterface.cpp:390).
// A stub ezt a ketto egyuttest modellezi.
enum IPType { IPv4, IPv6 };

class IPAddress : public Print {
public:
  IPAddress() { o[0]=o[1]=o[2]=o[3]=0; }
  IPAddress(uint32_t) { o[0]=o[1]=o[2]=o[3]=0; }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { o[0]=a;o[1]=b;o[2]=c;o[3]=d; }
  bool fromString(const char* s) {
    if (!s) return false;
    unsigned a,b,c,d; char extra;
    if (sscanf(s, "%u.%u.%u.%u%c", &a,&b,&c,&d,&extra) == 4
        && a<=255 && b<=255 && c<=255 && d<=255) {
      o[0]=(uint8_t)a;o[1]=(uint8_t)b;o[2]=(uint8_t)c;o[3]=(uint8_t)d;
      t_ = IPv4; return true;
    }
    return fromString6(s);
  }
  // Csak annyira reszletes, amennyire a viselkedes szamit: hexa csoportok
  // kettosponttal, legfeljebb egy "::" tomoritessel.
  bool fromString6(const char* s) {
    bool sawColon = false, sawDouble = false;
    for (const char* p = s; *p; p++) {
      if (*p == ':') {
        if (p[1] == ':') { if (sawDouble) return false; sawDouble = true; p++; }
        sawColon = true;
      } else if (!isxdigit((unsigned char)*p)) {
        return false;
      }
    }
    if (!sawColon) return false;
    o[0]=o[1]=o[2]=o[3]=0;
    t_ = IPv6; return true;
  }
  IPType type() const { return t_; }
  // IPAddress.h:83 - IPv6-ra szandekosan 0
  operator uint32_t() const {
    return t_ == IPv4 ? ((uint32_t)o[0]<<24 | (uint32_t)o[1]<<16 | (uint32_t)o[2]<<8 | o[3]) : 0u;
  }
  std::string str() const {
    if (t_ == IPv6) return "<ipv6>";
    char t[20]; snprintf(t,sizeof(t),"%u.%u.%u.%u",o[0],o[1],o[2],o[3]); return t;
  }
  bool isZero() const { return !o[0] && !o[1] && !o[2] && !o[3]; }
  uint8_t o[4];
private:
  IPType t_ = IPv4;
};

inline size_t Print::print(const IPAddress& a) { buf_ += a.str(); return 0; }
inline size_t Print::println(const IPAddress& a) { buf_ += a.str(); flushLine(); return 0; }

// A valodi getEfuseMac() az esp_efuse_mac_get_default()-tel 6 bajtot ir egy
// uint64_t-be (Esp.cpp), a felso 2 bajt nulla marad. A tesztekben allithato,
// hogy a "masik lapkan nem mukodik" viselkedes ellenorizheto legyen.
extern uint64_t g_efuseMac;

class EspClass {
public:
  void restart() { simLog("ESP.restart"); throw RestartSignal{}; }
  const char* getChipModel() { return "ESP32-C3"; }
  uint64_t getEfuseMac() { return g_efuseMac; }
};
extern EspClass ESP;
