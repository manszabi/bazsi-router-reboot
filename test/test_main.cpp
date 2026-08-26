#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPping.h>
#include <HTTPClient.h>
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
bool testInternetPing(IPAddress& target, const char* name);
enum ConfigStatus : uint8_t;
ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize);
bool writeConfigValue(fs::FS& fs, const char* path, const char* msg);
bool clearConfigValue(fs::FS& fs, const char* path);
bool initLittleFS();
bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len);
extern bool fsReady;
void resetbutton();
void wifiresetbutton();
void waitWithButtons(uint32_t);
extern IPAddress pingTargetCloudflare;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if(!(cond)) { printf("  \033[31mFAIL\033[0m %s\n", msg); failures++; } \
                              else printf("  ok   %s\n", msg); } while(0)

static int logIndex(const char* frag) {
  for (size_t i = 0; i < g_log.size(); i++) if (g_log[i].find(frag) != std::string::npos) return (int)i;
  return -1;
}

// Teljes újraindítás szimulálása (deep sleep ébredés is így viselkedik)
static void coldBoot(bool willConnect, const char* s, const char* p,
                     const char* ip, const char* gw, uint32_t latency = 500) {
  g_millis = 1; g_log.clear(); g_pinState.clear(); g_pinRead.clear();
  g_fs.clear(); g_fsMountOk = true; g_wakeupUs = 0;
  g_fsWritable = true; g_fsCapacity = 0; g_fsRemoveOk = true; g_fsReadable = true;
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
    try { for (int i = 0; i < 90000 && g_millis < 15u*60*1000; i++) loop(); }
    catch (DeepSleepSignal&) { slept = true; }
    catch (RestartSignal&) {}
    CHECK(!slept, "15 perc konfig módban sem alszik el (regresszió)");
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
    CHECK(deviceMode == (DeviceMode)1, "AP konfig portálra váltott"); }

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
    uint32_t t0 = g_millis; bool slept = false; uint64_t us = 0;
    try { reconnectWifi(); } catch (DeepSleepSignal& d) { slept = true; us = d.us; }
    CHECK(slept, "elalvás megtörtént");
    CHECK(wifiSim.beginCount == 3, "pontosan 3 csatlakozási kísérlet");
    CHECK(us == 3600ULL*1000000ULL, "1 órás timer ébresztés armolva");
    uint32_t dt = g_millis - t0;
    CHECK(dt >= 100000 && dt < 130000, "3x20s timeout + 2x20s várakozás (~100s)"); }

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
  { wifiSim.willConnect = false; wifiSim.begun = false;
    try { reconnectWifi(); } catch (DeepSleepSignal&) {}
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
  wifiSim.willConnect = false; wifiSim.begun = false;
  try { reconnectWifi(); } catch (DeepSleepSignal&) {}
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
  CHECK(testInternetPing(pingTargetCloudflare, "Test"), "sikeres");
  CHECK(pingSim.calls == 2, "csak 2 pinget futtatott a 4-ből (korai kilépés)");

}

static void sc20() {
  pingSim = PingSim(); pingSim.ok = false;
  CHECK(!testInternetPing(pingTargetCloudflare, "Test"), "sikertelen");
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
  // A normál (nem végzetes) elalvás viszont továbbra is időzítve ébred
  coldBoot(true, "TestNet", "pw", "", "");
  setup();
  wifiSim.willConnect = false; wifiSim.begun = false;
  uint64_t us = 0;
  try { reconnectWifi(); } catch (DeepSleepSignal& d) { us = d.us; }
  CHECK(us == 3600ULL * 1000000ULL, "1 órás ébresztés megmaradt a normál útvonalon");
}

struct Scenario { const char* name; void (*fn)(); };
static const Scenario kScenarios[] = {
  { "W1: nincs mentett SSID -> AP konfigurációs portál, NEM alszik el", sc0 },
  { "W2: sikeres csatlakozás -> monitor mód, DHCP (nincs statikus IP)", sc1 },
  { "W3: statikus IP -> config() DNS-sel, a mode() után", sc2 },
  { "W4: hibás IP formátum -> DHCP-re esik vissza, nem konfigurál", sc3 },
  { "W5: nem sikerül csatlakozni -> ~20s timeout, majd AP portál", sc4 },
  { "W6: initWiFi() nem bontja le a már élő kapcsolatot", sc5 },
  { "W7: router reset utáni újracsatlakozás megőrzi a statikus IP+DNS-t", sc6 },
  { "W8: reconnectWifi() 3 próba után deep sleepbe megy", sc7 },
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
