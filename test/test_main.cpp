#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPping.h>
#include <HTTPClient.h>
#include <esp_system.h>

#include <ESPAsyncWebServer.h>
extern std::map<std::string, ArRequestHandlerFunction> g_handlers;
#include <cassert>
#include <unistd.h>
#include <sys/wait.h>

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
extern uint32_t rtcRetryRounds;
extern uint32_t rtcEvMagic;
extern uint32_t rtcEvNext;
enum EventCode : uint8_t;
struct EventEntry { uint32_t uptimeSec; uint16_t param; uint8_t code; uint8_t pad; };
extern EventEntry rtcEvents[];
void logEvent(EventCode code, uint16_t param);
constexpr uint8_t EV_TEST_FAIL_C = 4;
constexpr uint8_t EV_FATAL_C = 9;
void resetbutton();
void wifiresetbutton();
void waitWithButtons(uint32_t);
void touchApDeadline();
void startConfigPortal();

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

static int logIndex(const char* frag) {
  for (size_t i = 0; i < g_log.size(); i++) if (g_log[i].find(frag) != std::string::npos) return (int)i;
  return -1;
}

// Teljes újraindítás szimulálása (deep sleep ébredés is így viselkedik)
// deepSleepWake = true: a deep sleep utáni ébredést modellezi, ahol az RTC
// memória tartalma (rtcConnectRounds) megmarad.
static void coldBoot(bool willConnect, const char* s, const char* p,
                     const char* ip, const char* gw, uint32_t latency = 500,
                     bool deepSleepWake = false) {
  g_millis = 1; g_log.clear(); g_serialLog.clear(); g_pinState.clear(); g_pinRead.clear();
  g_fs.clear(); g_fsMountOk = true; g_wakeupUs = 0;
  g_fsWritable = true; g_fsCapacity = 0; g_fsRemoveOk = true; g_fsReadable = true;
  g_resetReason = ESP_RST_POWERON;
  rtcRetryRounds = 0;
  g_gpioWakeMask = 0; g_gpioWakeMode = -1;
  g_httpCode = 200; g_httpSize = -2; g_httpBeginOk = true; g_httpBody = "Microsoft NCSI";
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
    CHECK(g_pinState[7] == LOW,  "relé (GPIO7) LOW - a router kap áramot");
    CHECK(g_pinState[6] == LOW,  "státusz LED (GPIO6) LOW");
    CHECK(g_pinState[5] == LOW,  "wifi LED (GPIO5) LOW");
    CHECK(!g_serialOn, "Serial.end() megtörtént");
    CHECK(logIndex("pin7=LOW") < logIndex("DEEP_SLEEP"), "a relé az alvás ELŐTT kapcsolt le");
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
    CHECK(logIndex("pin7=HIGH") >= 0, "relé bekapcsolt (router áramtalanítva)");
    CHECK(logIndex("pin7=HIGH") < logIndex("pin7=LOW"), "előbb be, aztán ki");
    CHECK(g_pinState[7] == LOW, "a végén a router visszakapta az áramot"); }

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
  coldBoot(false, "", "", "", "");
  g_fs.clear();                       // a data/ mappa nincs feltöltve
  try { setup(); } catch (DeepSleepSignal&) {}
  CHECK(deviceMode == (DeviceMode)1, "konfig portál elindult LittleFS tartalom nélkül is");
  CHECK(!g_fs.count("/wifimanager.html"), "tényleg nincs feltöltve a HTML");

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
  CHECK(g_pinState[7] == LOW, "a relé LOW - a router kap áramot");
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
  CHECK(g_pinState[7] == LOW, "a relé végig LOW maradt");

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
  CHECK(g_pinState[7] == LOW, "a relé LOW - a router kap áramot alvás közben is");
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
  CHECK(g_pinState[7] == LOW, "a relé LOW - a router kap áramot");
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
  CHECK(wifiSim.beginCount - beginBefore == 6, "3 próba, router reset, majd újabb 3");
  CHECK(logIndex("pin7=HIGH") >= 0, "a körben lefutott egy router újraindítás");
  const uint32_t dt = g_millis - t0;
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
  try { while (logIndex("pin7=HIGH") < 0 && ++guard < 200000) loop(); }
  catch (DeepSleepSignal&) {}
  CHECK(wifiSim.beginCount - beginBefore == 3, "3 újrapróbálkozás a reset előtt");
  CHECK(logIndex("pin7=HIGH") >= 0, "elindult a router újraindítás (relé be)");
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
  CHECK(g_pinState[7] == LOW, "a relé LOW - a router kap áramot");
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
  while (logIndex("pin7=HIGH") < 0 && ++guard < 300000) { loop(); g_millis += 10; }
  CHECK(guard < 300000, "eljutott a router resetig");
  const int relayOn = logIndex("pin7=HIGH");
  const int relayOff = logIndex("pin7=LOW");
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
  CHECK(logIndex("pin7=HIGH") >= 0, "közben lefutott egy router újraindítás");
  CHECK(g_pinState[7] == LOW, "a relé a végén LOW - a router kap áramot");
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
  CHECK(g_fs["/pass.txt"] == "titkosjelszo", "jelszó kiírva");
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
  // Érvénytelen IP formátum
  coldBoot(false, "", "", "", "");
  setup();
  std::string body;
  const int code = postConfig("MyNetwork", "jelszo", "nem-ip-cim", "", &body);
  CHECK(code == 500, "HTTP 500 rossz IP esetén");
  CHECK(!restartPending, "nem indul újra");
  CHECK(body.find("IP") != std::string::npos, "a válasz megnevezi az okot");
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
  CHECK(g_pinState[7] == HIGH, "a relé ekkor még HIGH volt (router áram nélkül)");
  // az újraindulás után a setup() azonnal áramot ad a routernek
  g_pinRead[3] = HIGH;
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  CHECK(g_pinState[7] == LOW, "az újraindulás után a relé LOW - a router kap áramot");
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
  CHECK(g_fs.count("/pass.txt") && g_fs["/pass.txt"].empty(), "üres jelszó kiírva");
}

static void scX3() {
  // Határértékek: pontosan 32 karakteres SSID és 63 karakteres jelszó
  coldBoot(false, "", "", "", "");
  setup();
  std::string s32(32, 'A'), s63(63, 'B');
  const int code = postConfig(s32.c_str(), s63.c_str(), "", "");
  CHECK(code == 200, "a pontos maximum még elfogadott");
  CHECK(g_fs["/ssid.txt"].size() == 32, "32 karakteres SSID hiánytalanul mentve");
  CHECK(g_fs["/pass.txt"].size() == 63, "63 karakteres jelszó hiánytalanul mentve");

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
  // Rossz jelszó (hitelesítési hiba) -> AZONNAL AP mód, nem 2 nap várakozás
  coldBoot(false, "MyNetwork", "rosszjelszo", "", "");
  wifiSim.failStatus = WL_CONNECT_FAILED;
  setup();
  g_log.clear();
  bool slept = false;
  const uint32_t t0 = g_millis;
  try { while (g_millis - t0 < 20u*60*1000 && deviceMode == (DeviceMode)0) loop(); }
  catch (DeepSleepSignal&) { slept = true; }
  CHECK(!slept, "nem aludt el");
  CHECK(deviceMode == (DeviceMode)1, "AP beállító módba ment");
  CHECK(rtcRetryRounds == 0, "nem számolt újrapróbálkozási kört");
  CHECK(logIndex("pin7=HIGH") < 0, "rossz jelszónál NEM indítja újra a routert");
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
  CHECK(g_pinState[7] == LOW, "a relé LOW - a router kap áramot");
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
  { "R1: a reset pulzus tényleg 90 másodperc (regresszió a fő hibára)", sc13 },
  { "R2: az 5. reset esemény deep sleepet vált ki", sc14 },
  { "H1: a záró CR/LF nem buktatja el az egyezést", sc15 },
  { "H2: eltérő tartalom és hibás státusz elbukik", sc16 },
  { "H3: captive portal nagy válaszát el sem olvassa", sc17 },
  { "H4: ismeretlen hosszú (chunked) válasz is korlátozva olvasódik", sc18 },
  { "P1: 2 sikeres ping után korán kilép", sc19 },
  { "P2: csupa sikertelen ping - a 3. után eldől", sc20 },
  { "C1: konfig írás/olvasás oda-vissza, csonkítással", sc21 },
  { "B1: egyetlen zajtüske nem indítja újra az eszközt", sc22 },
  { "F1: hiányzó wifimanager.html esetén is van beállító űrlap", sc23 },
  { "WDT1: a watchdog tényleg újraindít, nem csak figyelmeztet", scWDT1 },
  { "WDT2: a 90 mp-es relé pulzus alatt is etetve van", scWDT2 },
  { "WDT3: a ~100 mp-es újracsatlakozás alatt is etetve van", scWDT3 },
  { "WDT4: a hosszú várakozás delay()-jel megy, nem CPU-pörgetéssel", scWDT4 },
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
