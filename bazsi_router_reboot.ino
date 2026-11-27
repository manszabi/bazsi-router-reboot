// Router-újraindító: internetteszt, és ha kell, a router áramtalanítása reléval (XIAO ESP32-C3).
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "LittleFS.h"
#include "esp_sleep.h"
#include "esp_idf_version.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include <time.h>
#include "driver/gpio.h"
#include "limits_config.h"
#include "sync.h"
#include "eventlog.h"
#include "configstore.h"
#include "webportal.h"
#include "strutil.h"
#include "secret.h"
#include "app_hooks.h"
#include "netprobe.h"

// Lábak (XIAO ESP32-C3). A D0 (GPIO2) strapping láb: bekapcsoláskor ne legyen lenyomva.
constexpr uint8_t ledPin = D4;
constexpr uint8_t wifiledPin = D3;

// HIGH = a router áram nélkül; külső 22k lehúzó tartja LOW-n a boot alatt.
constexpr uint8_t relayPin = D10;
constexpr uint8_t wifiresetPin = D0;
constexpr uint8_t resetPin = D1;

// Időzítések.
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20 * 1000;
// Ilyen sűrűn nézzük, van-e internet (normál üzemben és a hosszú várakozások alatt is).
constexpr uint32_t CHECK_INTERVAL_MS = 60 * 1000;

// Ennyi internet nélküli idő után reset: ha a router sem válaszol, illetve ha válaszol.
constexpr uint32_t ROUTER_DOWN_WINDOW_MS = 2 * 60 * 1000;
constexpr uint32_t WAN_DOWN_WINDOW_MS = 15 * 60 * 1000;

// Éjszakai csendes időszak (helyi idő, pl. firmware-frissítés): a router-hiba miatti reset csak 10 perc után.
constexpr int QUIET_START_HOUR = 22;
constexpr int QUIET_END_HOUR = 5;
constexpr uint32_t QUIET_WINDOW_MS = 10 * 60 * 1000;

// Ha a reset nem segített, a következő ennyivel az előző után jön (az utolsó ismétlődik).
constexpr uint32_t RETRY_BACKOFF_MS[] = { 30 * 60 * 1000, 60 * 60 * 1000,
                                          120 * 60 * 1000, 240 * 60 * 1000 };
constexpr uint8_t RETRY_BACKOFF_STEPS = sizeof(RETRY_BACKOFF_MS) / sizeof(RETRY_BACKOFF_MS[0]);

// A router DNS-e: 5 percenként, gyanúnál percenként; 10 perc folyamatos hiba után reset.
constexpr uint32_t DNS_CHECK_INTERVAL_MS = 5 * 60 * 1000;
constexpr uint32_t DNS_FAIL_WINDOW_MS = 10 * 60 * 1000;
constexpr char DNS_PROBE_NAME[] = "connectivitycheck.gstatic.com";
// Várakozás a router bootolására (korábban véget ér, ha a hálózat visszajött).
constexpr uint32_t RESET_DELAY = 10 * 60 * 1000;
constexpr uint32_t RESET_PULSE = 90 * 1000;
constexpr uint32_t firstStartDelay = 10 * 60 * 1000;

// Wi-Fi újracsatlakozás: 3 próba, köztük 30 mp.
constexpr uint8_t wifi_maxRetries = 3;
constexpr uint32_t wifiInterval = 30 * 1000;

// Wi-Fi-kiesés: 30 perc stabil kapcsolat nullázza a lépcsőt, 48 óra után AP beállító mód.
constexpr uint32_t WIFI_STABLE_MS = 30 * 60 * 1000;
constexpr uint32_t WIFI_GIVEUP_MIN = 48 * 60;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t BUTTON_POLL_MS = 10;

// LED-jelzések: végzetes hiba 5 Hz, beragadt gomb felváltva, AP 1 Hz, reset 2 Hz.
constexpr uint32_t FATAL_BLINK_MS = 100;
constexpr uint32_t FATAL_SLEEP_AFTER_MS = 5 * 60 * 1000;
constexpr uint32_t STUCK_BLINK_MS = 3000;
constexpr uint32_t AP_BLINK_MS = 500;
constexpr uint32_t RESET_BLINK_MS = 250;

// A leghosszabb etetés nélküli blokkolás (halott DNS melletti http.GET) ~33 mp.
constexpr uint32_t WDT_TIMEOUT_MS = 90 * 1000;

// Ennyi rendellenes újraindulás után végzetes hiba; 1 óra hibátlan futás nulláz.
constexpr uint32_t MAX_WDT_RESETS = 3;
constexpr uint32_t WDT_COUNTER_CLEAR_MS = 60 * 60 * 1000;

// Alvás: biztonsági időzítő, ha a gombos ébresztés nem élesíthető; beragadt gombnál 60 mp.
constexpr uint64_t SLEEP_DURATION_US = 3600ULL * 1000000ULL;
constexpr uint64_t STUCK_BUTTON_SLEEP_US = 60ULL * 1000000ULL;

// Ébresztő gombnyomásnál a gomb még lent van: ennyit várunk az elengedésre.
constexpr uint32_t STUCK_BUTTON_CONFIRM_MS = 3000;

// Heap-felügyelet: figyelmeztetés, majd tartósan kritikus szintnél önkéntes újraindulás.
constexpr uint32_t HEAP_CHECK_INTERVAL_MS = 10 * 1000;
constexpr uint32_t HEAP_LOG_INTERVAL_MS   = 30 * 60 * 1000;
constexpr uint32_t HEAP_WARN_BYTES  = 25000;
constexpr uint32_t HEAP_CRIT_BYTES  = 12000;
constexpr uint32_t HEAP_CRIT_BLOCK_BYTES = 6000;
constexpr uint8_t HEAP_CRIT_SAMPLES = 3;
constexpr uint32_t MAX_HEAP_RESTARTS = 3;

// Két független üzemeltető (Cloudflare, Google): az internet elérhető, ha bármelyik válaszol.
constexpr uint8_t PROBE_PING_IP[4] = { 1, 1, 1, 1 };
constexpr uint8_t PROBE_PING_IP2[4] = { 8, 8, 8, 8 };

// Tartalék HTTP teszt (ha a ping szűrve van): öt független üzemeltető, sorban váltva.
struct TestEndpoint {
  const char* url;
  const char* expect;
};
constexpr TestEndpoint TEST_ENDPOINTS[] = {
  { "http://www.msftconnecttest.com/connecttest.txt",    "Microsoft Connect Test"   },
  { "http://cp.cloudflare.com/generate_204",             ""                         },
  { "http://detectportal.firefox.com/success.txt",       "success"                  },
  { "http://nmcheck.gnome.org/check_network_status.txt", "NetworkManager is online" },
  { "http://connectivitycheck.gstatic.com/generate_204", ""                         },
};
constexpr uint8_t ENDPOINT_COUNT = sizeof(TEST_ENDPOINTS) / sizeof(TEST_ENDPOINTS[0]);

// A folyamatban lévő kiesés: kezdete, az eddigi resetek száma és az utolsó reset ideje.
struct TestState {
  uint8_t endpointIndex = 0;
  uint8_t resetEvents = 0;
  uint8_t resetStep = 0;
  bool outage = false;
  uint32_t outageStart = 0;
  uint32_t lastResetAt = 0;
};

struct TimingState {
  uint32_t stateStart = 0;
  uint32_t resetPulseStart = 0;
  uint32_t startMillis = 0;
  uint32_t resetBtnDownSince = 0;
  uint32_t wifiResetBtnDownSince = 0;
  uint32_t blinkLast = 0;
  uint32_t fatalStart = 0;
};

struct UIFlags {
  bool firstStartPrinted = false;
  bool firstStart = true;
  bool blinkOn = false;
};

// A router DNS-ének állapota; saját reset-lépcső, mert a ping közben végig sikeres.
struct DnsState {
  bool checked = false;
  uint32_t lastCheck = 0;
  bool suspect = false;
  uint32_t suspectSince = 0;
  uint8_t resetEvents = 0;
  uint32_t lastResetAt = 0;
  bool skipLogged = false;
};

TestState testState;
DnsState dnsState;
TimingState timing;
UIFlags uiFlags;

enum State : uint8_t {
  CHECK_STATE = 0,
  WAIT_STATE = 1,
  RESET_STATE = 2
};

enum DeviceMode : uint8_t {
  MODE_MONITOR = 0,
  MODE_CONFIG = 1,
  MODE_FATAL = 2
};

State currentState = CHECK_STATE;
DeviceMode deviceMode = MODE_MONITOR;

// Igaz, ha a WiFi.config() statikus IP-t állított be.
bool staticConfigActive = false;

// Gomb-reteszek: az ISR a lenyomás hosszát méri, így blokkoló hívás alatt sem vész el nyomás.
volatile bool btnResetLatched = false;
volatile bool btnWifiResetLatched = false;
volatile uint32_t btnResetDownAt = 0;
volatile uint32_t btnWifiResetDownAt = 0;

// A korai kilépés próbáinak órái.
uint32_t resetDelayProbeLast = 0;
uint32_t firstStartProbeLast = 0;

// Heap-felügyelet állapota.
uint32_t heapCheckLast = 0;
uint32_t heapLogLast = 0;
uint8_t  heapCritStreak = 0;
bool     heapWarnActive = false;
bool watchdogEnabled = false;

// RTC_NOINIT: a watchdog-resetet is túléli; a magic szűri a bekapcsoláskori szemetet.
constexpr uint32_t WDT_COUNTER_MAGIC = 0x42415A53UL;
RTC_NOINIT_ATTR uint32_t rtcWdtMagic;
RTC_NOINIT_ATTR uint32_t rtcWdtResets;

// A Wi-Fi-lépcső (RTC_NOINIT): a saját újraindítást túléli; áramszünet, gomb és alvás törli.
constexpr uint32_t WIFI_LADDER_MAGIC = 0x42415A57UL;
RTC_NOINIT_ATTR uint32_t rtcWifiMagic;
RTC_NOINIT_ATTR uint8_t  rtcWifiStep;
RTC_NOINIT_ATTR uint32_t rtcWifiOutageMin;

// A lépcső RAM-beli órái: a kiesés kezdete, az utolsó router reset, a stabil kapcsolat kezdete.
uint32_t wifiOutageSince = 0;
uint32_t wifiLastResetAt = 0;
uint32_t wifiConnectedSince = 0;

// A reset oka (a NET DIAG esemény paramétere).
enum NetDiag : uint8_t {
  DIAG_ROUTER = 1,
  DIAG_WAN = 2,
  DIAG_DNS = 3
};

DiagCounters diagCounters() {
  return DiagCounters{ rtcWdtResets, MAX_WDT_RESETS, rtcWifiStep, rtcWifiOutageMin };
}

// Heap miatti újraindulásnál átvitt állapot (RTC_NOINIT, egyszer használjuk fel).
constexpr uint32_t HEAP_MAGIC = 0x42415A48UL;
constexpr uint8_t  CARRY_NONE = 0xFF;
RTC_NOINIT_ATTR uint32_t rtcHeapMagic;
RTC_NOINIT_ATTR uint32_t rtcHeapRestarts;
RTC_NOINIT_ATTR uint8_t  rtcCarryResetEvents;

// Előre deklarációk.
void printUptime();
void resetbutton();
void wifiresetbutton();
void armButtonLatches();
void restartFromButton(const char* reason);
void doWifiReset();
void blockingDelay(uint32_t duration);
void waitWithButtons(uint32_t duration);
bool onlineProbe();
bool internetOk();
bool onlineProbeDue(uint32_t& lastProbe, uint32_t now);
void lockConfigBeforeShutdown();
void fatalSleep();
void apSleep();
void feedWatchdog();
void wifiFailed(bool coldStart);
void wifiLadderClear();
void wifiLadderPersist();
void startRouterReset(uint8_t diag);
uint32_t backoffMs(uint8_t resetek);
bool quietHours();
bool wifiAuthFailed();
bool reset_device();
void startConfigPortal();
void enterFatal(const char* reason);
void fatalHalt(const char* reason);
void enterDeepSleep(uint64_t timerUs);
void holdRelayForSleep();
int pressedButtonNow();
void checkHeap(uint32_t now);
void initHeapState();
void applyHeapCarry();
bool initWiFi();
bool reconnectWifi();
bool gatewayUnreachable();
bool gatewayPingOk();
void ensureFallbackDns();

void enterFatal(const char* reason) {
  deviceMode = MODE_FATAL;
  timing.fatalStart = millis();
  timing.blinkLast = timing.fatalStart;
  digitalWrite(relayPin, LOW);
  Serial.println();
  Serial.println("!!! VEGZETES HIBA !!!");
  Serial.println(reason);
  Serial.println("A program leallt, mindket LED gyorsan villog.");
  Serial.println("Reset gomb: ujrainditas. Wifireset gomb: mentett adatok torlese.");
  Serial.print(FATAL_SLEEP_AFTER_MS / 60000);
  Serial.println(" perc mulva az eszkoz elalszik (magatol nem ebred fel).");
}

// A heap-átvitel RTC blokkjának inicializálása bekapcsolás után.
void initHeapState() {
  if (rtcHeapMagic != HEAP_MAGIC) {
    rtcHeapMagic = HEAP_MAGIC;
    rtcHeapRestarts = 0;
    rtcCarryResetEvents = CARRY_NONE;
  }
}

// Az átvitt állapot visszaállítása - pontosan egyszer.
void applyHeapCarry() {
  if (rtcCarryResetEvents == CARRY_NONE) {
    return;
  }
  testState.resetEvents = rtcCarryResetEvents;
  rtcCarryResetEvents = CARRY_NONE;
  // Ha a kiesés már kapott resetet, a következő a lépcső szerint az indulástól számít.
  if (testState.resetEvents > 0) {
    testState.outage = true;
    testState.outageStart = millis();
    testState.lastResetAt = testState.outageStart;
  }
  printUptime();
  Serial.print("Heap miatti ujraindulas utan folytatjuk: router reset szamlalo = ");
  Serial.println(testState.resetEvents);
}

// Heap mérése, ritkított kiírás, tartósan kritikus szintnél önkéntes újraindulás.
void checkHeap(uint32_t now) {
  if (now - heapCheckLast < HEAP_CHECK_INTERVAL_MS) {
    return;
  }
  heapCheckLast = now;

  const uint32_t szabad = ESP.getFreeHeap();
  const uint32_t tomb   = ESP.getMaxAllocHeap();

  if (now - heapLogLast >= HEAP_LOG_INTERVAL_MS) {
    heapLogLast = now;
    printUptime();
    Serial.printf("Heap: szabad %u B, legnagyobb tomb %u B, valaha volt legkisebb %u B\n",
                  (unsigned)szabad, (unsigned)tomb, (unsigned)ESP.getMinFreeHeap());
  }

  if (!heapWarnActive && szabad < HEAP_WARN_BYTES) {
    heapWarnActive = true;

    if (!lastEventWas((uint8_t)EV_LOW_HEAP)) {
      logEvent(EV_LOW_HEAP, (uint16_t)(szabad / 1024));
    }
    printUptime();
    Serial.printf("FIGYELEM: alacsony a szabad heap (%u B), a kuszob %u B.\n",
                  (unsigned)szabad, (unsigned)HEAP_WARN_BYTES);
  } else if (heapWarnActive && szabad > HEAP_WARN_BYTES + HEAP_WARN_BYTES / 10) {
    heapWarnActive = false;
    printUptime();
    Serial.printf("A szabad heap visszaallt (%u B).\n", (unsigned)szabad);
  }

  const bool kritikus = (szabad < HEAP_CRIT_BYTES) || (tomb < HEAP_CRIT_BLOCK_BYTES);
  if (!kritikus) {
    heapCritStreak = 0;
    return;
  }

  if (heapCritStreak < HEAP_CRIT_SAMPLES) {
    heapCritStreak++;
    return;
  }

  if (deviceMode != MODE_MONITOR || configWriteInProgress() || restartRequested()
      || testState.resetStep != 0 || currentState == RESET_STATE) {
    return;
  }

  if (rtcHeapRestarts >= MAX_HEAP_RESTARTS) {
    logEvent(EV_FATAL, 5);
    enterFatal("Tartosan keves a szabad heap - az ujrainditas sem segitett.");
    return;
  }

  rtcCarryResetEvents = testState.resetEvents;
  rtcHeapRestarts++;
  logEvent(EV_HEAP_RESTART, (uint16_t)(szabad / 1024));

  saveEventLog("heap miatti ujraindulas elott");
  wifiLadderPersist();

  printUptime();
  Serial.printf("KRITIKUS: a szabad heap %u B (legnagyobb tomb %u B) %u meresen at.\n",
                (unsigned)szabad, (unsigned)tomb, (unsigned)HEAP_CRIT_SAMPLES);
  Serial.print("Onkentes ujraindulas, sorszam: ");
  Serial.print(rtcHeapRestarts);
  Serial.print(" / ");
  Serial.println(MAX_HEAP_RESTARTS);

  lockConfigBeforeShutdown();
  Serial.flush();
  ESP.restart();
}

constexpr size_t CMD_BUF_LEN = 16;
static char cmdBuf[CMD_BUF_LEN];
static uint8_t cmdLen = 0;

static bool cmdOverflow = false;

// Soros parancsok: LOG (a teljes napló), HELP.
static void runSerialCommand(const char* cmd) {
  if (cmd[0] == '/') {
    cmd++;
  }
  if (strcasecmp(cmd, "LOG") == 0) {
    printEventLogs();
  } else if (strcasecmp(cmd, "HELP") == 0 || strcasecmp(cmd, "?") == 0) {
    Serial.println();
    Serial.println("Parancsok:");
    Serial.println("  LOG   - a teljes esemenynaplo (fajl + RTC) kiirasa");
    Serial.println("  HELP  - ez a lista");
    Serial.println();
  } else if (cmd[0] != '\0') {
    Serial.print("Ismeretlen parancs: ");
    Serial.println(cmd);
    Serial.println("Probald a HELP-et.");
  }
}

// Nem blokkol: csak a már beérkezett bájtokat dolgozza fel; túl hosszú sort egészben eldob.
void serialCommands() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\n' || c == '\r') {
      if (cmdOverflow) {
        Serial.println("Tul hosszu parancs, eldobva.");
      } else if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        runSerialCommand(cmdBuf);
      }
      cmdLen = 0;
      cmdOverflow = false;
    } else if (cmdLen < CMD_BUF_LEN - 1) {
      cmdBuf[cmdLen++] = (char)c;
    } else {
      cmdOverflow = true;
      cmdLen = 0;
    }
  }
}

// Rendellenes újraindulások számlálása; tápellátási ok nulláz, USB/JTAG nem.
void checkWatchdogResets() {
  const esp_reset_reason_t reason = esp_reset_reason();

  if (rtcWdtMagic != WDT_COUNTER_MAGIC) {
    rtcWdtMagic = WDT_COUNTER_MAGIC;
    rtcWdtResets = 0;
  }

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
      rtcWdtResets = 0;
      logEvent(EV_FATAL, 3);
      enterFatal("Tul sok watchdog miatti ujrainditas - a program instabil.");
    }
  } else if (reason == ESP_RST_POWERON || reason == ESP_RST_EXT
             || reason == ESP_RST_BROWNOUT) {
    rtcWdtResets = 0;
  }
}

// Task watchdog a loop taskra: előbb feliratkozás, aztán konfigurálás; hibánál nem állít védelmet.
void initWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WDT_TIMEOUT_MS;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;

  esp_err_t initErr = ESP_OK;
  if (esp_task_wdt_status(NULL) == ESP_ERR_INVALID_STATE) {
    initErr = esp_task_wdt_init(&cfg);
  }

  if (initErr == ESP_OK) {
    enableLoopWDT();
  }

  const esp_err_t cfgErr = (initErr == ESP_OK) ? esp_task_wdt_reconfigure(&cfg) : initErr;

  watchdogEnabled = (esp_task_wdt_status(NULL) == ESP_OK);

  if (!watchdogEnabled || cfgErr != ESP_OK) {
    if (watchdogEnabled) {
      disableLoopWDT();
      watchdogEnabled = (esp_task_wdt_status(NULL) == ESP_OK);
    }
    Serial.print("FIGYELEM: a watchdog NEM vedi a loop()-ot (feliratkozas ");
    Serial.print(watchdogEnabled ? "OK" : "SIKERTELEN/VISSZAVONVA");
    Serial.print(", hibakod ");
    Serial.print((int)cfgErr);
    Serial.println("). A program fut, de lefagyas eseten nem indul ujra.");
    return;
  }
  Serial.print("Watchdog enabled, timeout ");
  Serial.print(WDT_TIMEOUT_MS / 1000);
  Serial.println(" s");
}

// Gomb-ISR-ek: csak a lenyomás hosszát mérik és reteszelnek, semmi mást.
void IRAM_ATTR onResetButtonEdge() {
  if (digitalRead(resetPin) == LOW) {
    const uint32_t now = millis();
    btnResetDownAt = (now != 0) ? now : 1;
    return;
  }

  const uint32_t down = btnResetDownAt;
  btnResetDownAt = 0;
  if (down != 0 && millis() - down >= BUTTON_DEBOUNCE_MS) {
    btnResetLatched = true;
  }
}

void IRAM_ATTR onWifiResetButtonEdge() {
  if (digitalRead(wifiresetPin) == LOW) {
    const uint32_t now = millis();
    btnWifiResetDownAt = (now != 0) ? now : 1;
    return;
  }
  const uint32_t down = btnWifiResetDownAt;
  btnWifiResetDownAt = 0;
  if (down != 0 && millis() - down >= BUTTON_DEBOUNCE_MS) {
    btnWifiResetLatched = true;
  }
}

void armButtonLatches() {
  attachInterrupt(digitalPinToInterrupt(resetPin), onResetButtonEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(wifiresetPin), onWifiResetButtonEdge, CHANGE);
}

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

void waitWithButtons(uint32_t duration) {
  const uint32_t start = millis();
  while (millis() - start < duration) {
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
}

// Van internet, ha az 1.1.1.1, a 8.8.8.8 vagy egy HTTP végpont válaszol.
bool internetOk() {
  const IPAddress elso(PROBE_PING_IP[0], PROBE_PING_IP[1], PROBE_PING_IP[2], PROBE_PING_IP[3]);
  if (testInternetPing(elso, "internet")) {
    return true;
  }
  const IPAddress masodik(PROBE_PING_IP2[0], PROBE_PING_IP2[1], PROBE_PING_IP2[2], PROBE_PING_IP2[3]);
  if (testInternetPing(masodik, "internet (tartalek)")) {
    return true;
  }
  ensureFallbackDns();
  const TestEndpoint& vegpont = TEST_ENDPOINTS[testState.endpointIndex];
  testState.endpointIndex = (uint8_t)((testState.endpointIndex + 1) % ENDPOINT_COUNT);
  Serial.print("HTTP teszt: ");
  Serial.println(vegpont.url);
  return testInternetHTTP(vegpont.url, vegpont.expect);
}

// Wi-Fi nélkül csak aszinkron újracsatlakozást kezdeményez.
bool onlineProbe() {
  if (WiFi.status() != WL_CONNECTED) {
    if (ssid[0] != '\0') {
      WiFi.begin(ssid, pass);
    }

    return false;
  }

  return internetOk();
}

bool onlineProbeDue(uint32_t& lastProbe, uint32_t now) {
  if (now - lastProbe < CHECK_INTERVAL_MS) {
    return false;
  }
  lastProbe = now;
  return onlineProbe();
}

// Alvás/újraindulás előtt ennyit várunk egy folyamatban lévő mentésre.
constexpr uint32_t SAVE_WAIT_MAX_MS = 5000;

// Megvárja a mentést ÉS megszerzi a zárat, hogy leállás előtt ne indulhasson új.
void lockConfigBeforeShutdown() {
  if (beginConfigWrite()) {
    return;
  }
  printUptime();
  Serial.println("Fajliras folyik - megvarjuk, mielott alszunk vagy ujraindulunk.");
  const uint32_t start = millis();
  bool acquired = false;
  while (millis() - start < SAVE_WAIT_MAX_MS) {
    feedWatchdog();
    delay(BUTTON_POLL_MS);
    if (beginConfigWrite()) {
      acquired = true;
      break;
    }
  }
  if (acquired) {
    Serial.println("A fajliras befejezodott.");
  } else {
    Serial.print("FIGYELEM: a mentes ");
    Serial.print(SAVE_WAIT_MAX_MS / 1000);
    Serial.println(" mp alatt sem fejezodott be, tovabblepunk.");
  }
}

// A relé lábának LOW-n tartása deep sleep alatt.
void holdRelayForSleep() {
  if (gpio_hold_en((gpio_num_t)relayPin) == ESP_OK) {
    gpio_deep_sleep_hold_en();
  } else {
    Serial.println("FIGYELEM: a rele lab rogzitese (gpio_hold_en) nem sikerult.");
  }
}

int pressedButtonNow() {
  if (digitalRead(resetPin) == LOW) return 0;
  if (digitalRead(wifiresetPin) == LOW) return 1;
  return -1;
}

// Beragadt gomb: jelzés, majd 60 mp alvás gombébresztés nélkül (különben boot loop).
void handleStuckButton(const char* message, uint16_t which, bool logIt) {
  Serial.println(message);
  Serial.print("Alvas ");
  Serial.print((unsigned long)(STUCK_BUTTON_SLEEP_US / 1000000ULL));
  Serial.println(" masodpercre, utana ujraprobalkozas.");
  Serial.flush();
  if (logIt) {
    logEvent(EV_STUCK_BUTTON, which);
  }

  const uint32_t start = millis();
  bool on = false;
  // WDT-OK: korlátos, STUCK_BLINK_MS (3 mp) hosszú.

  while (millis() - start < STUCK_BLINK_MS) {
    on = !on;
    digitalWrite(ledPin, on ? HIGH : LOW);
    digitalWrite(wifiledPin, on ? LOW : HIGH);
    delay(FATAL_BLINK_MS);
  }
  digitalWrite(ledPin, LOW);
  digitalWrite(wifiledPin, LOW);

  holdRelayForSleep();

  Serial.flush();
  Serial.end();
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

// Csatlakozás (statikus IP-vel vagy DHCP-vel), legfeljebb WIFI_CONNECT_TIMEOUT_MS-ig.
bool initWiFi() {
  printUptime();
  if (ssid[0] == '\0') {
    Serial.println("Undefined SSID!");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected, skipping reconnect.");
    return true;
  }

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  bool staticOk = false;
  IPAddress localIP;
  IPAddress localGateway;
  if (ipStr[0] != '\0' || gatewayStr[0] != '\0') {
    const bool ipValid = localIP.fromString(ipStr) && isUsableIPv4(localIP);
    const bool gatewayValid = localGateway.fromString(gatewayStr) && isUsableIPv4(localGateway);
    if (!ipValid) {
      Serial.println("❌ Invalid IP format (csak IPv4 hasznalhato)!");
    }
    if (!gatewayValid) {
      Serial.println("❌ Invalid gateway format (csak IPv4 hasznalhato)!");
    }
    staticOk = ipValid && gatewayValid;

    if (staticOk && !staticPairValid(localIP, localGateway)) {
      Serial.println("❌ Az IP es a gateway nem illik ossze (nem ugyanaz a /24,");
      Serial.println("   vagy halozati/broadcast cim, vagy azonosak) - DHCP-t hasznalunk.");
      staticOk = false;
    }
  }

  if (staticOk) {
    const IPAddress subnet(255, 255, 255, 0);
    const IPAddress dnsFallback(1, 1, 1, 1);
    if (!WiFi.config(localIP, localGateway, subnet, localGateway, dnsFallback)) {
      Serial.println("⚠️ STA Failed to configure");
      staticConfigActive = false;
    } else {
      Serial.println("✅ Manual IP config applied.");
      staticConfigActive = true;
    }
  } else {
    staticConfigActive = false;
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
    if (millis() - startAttempt >= WIFI_CONNECT_TIMEOUT_MS) {
      printUptime();
      Serial.println("Failed to connect.");
      return false;
    }
  }

  ensureFallbackDns();

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

// A router áramtalanítása RESET_PULSE-ig; igazat ad, amikor a pulzus véget ért.
bool reset_device() {
  if (testState.resetStep == 0) {
    if (testState.resetEvents < 255) {
      testState.resetEvents++;
    }
    testState.lastResetAt = millis();
    logEvent(EV_ROUTER_RESET, (uint16_t)testState.resetEvents);

    saveEventLog("router reset elott");
    Serial.println("Router resetting");
    Serial.print("Powering OFF the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, HIGH);
    Serial.println("Relay on.");

    digitalWrite(ledPin, LOW);
    timing.blinkLast = millis();
    uiFlags.blinkOn = false;

    digitalWrite(wifiledPin, LOW);
    Serial.println("Reset_pulse delay.");
    testState.resetStep = 1;
    timing.resetPulseStart = millis();

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

  if (millis() - timing.blinkLast >= RESET_BLINK_MS) {
    timing.blinkLast = millis();
    uiFlags.blinkOn = !uiFlags.blinkOn;
    digitalWrite(ledPin, uiFlags.blinkOn ? HIGH : LOW);
  }
  return false;
}

// Közös elalvás: napló mentése, zár, relé LOW-n rögzítve; timerUs = 0 -> csak a reset gomb ébreszt.
void enterDeepSleep(uint64_t timerUs) {
  saveEventLog("alvas elott");
  wifiLadderClear();

  lockConfigBeforeShutdown();
  digitalWrite(ledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(wifiledPin, LOW);

  holdRelayForSleep();
  WiFi.disconnect(true);
  stopWebPortal();
  Serial.flush();
  Serial.end();

  detachInterrupt(digitalPinToInterrupt(resetPin));
  detachInterrupt(digitalPinToInterrupt(wifiresetPin));

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  esp_err_t gpioErr = ESP_FAIL;
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP

#if ESP_IDF_VERSION_MAJOR >= 6
  gpioErr = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << resetPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  gpioErr = esp_deep_sleep_enable_gpio_wakeup(1ULL << resetPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
#endif

  if (timerUs == 0 && gpioErr != ESP_OK) {
    timerUs = SLEEP_DURATION_US;
  }
  if (timerUs > 0) {
    esp_sleep_enable_timer_wakeup(timerUs);
  }
  esp_deep_sleep_start();
}

bool wifiAuthFailed() {
  return WiFi.status() == WL_CONNECT_FAILED;
}

// A kiesés eddigi hossza percben (a mentett rész + az aktuális indulás óta eltelt).
uint32_t wifiOutageMinutes() {
  return rtcWifiOutageMin + (millis() - wifiOutageSince) / 60000;
}

// A lépcső mentése az RTC-be (a saját és a heap miatti újraindulás előtt).
void wifiLadderPersist() {
  if (rtcWifiStep == 0) {
    return;
  }
  rtcWifiOutageMin = wifiOutageMinutes();
  wifiOutageSince = millis();
}

void wifiLadderClear() {
  rtcWifiMagic = WIFI_LADDER_MAGIC;
  rtcWifiStep = 0;
  rtcWifiOutageMin = 0;
  wifiConnectedSince = 0;
}

// Indulás: a mentett lépcső csak érvényes magic mellett él; az órák az indulástól számítanak.
void initWifiLadder() {
  if (rtcWifiMagic != WIFI_LADDER_MAGIC) {
    wifiLadderClear();
  }
  wifiOutageSince = millis();
  wifiLastResetAt = wifiOutageSince;
  wifiConnectedSince = 0;
  if (rtcWifiStep > 0) {
    printUptime();
    Serial.print("Wi-Fi lepcso folytatodik: ");
    Serial.print(rtcWifiStep);
    Serial.print(". fok, kieses ");
    Serial.print(rtcWifiOutageMin);
    Serial.println(" perce.");
  }
}

// Van Wi-Fi: 30 perc stabil kapcsolat után a lépcső nullázódik.
void wifiConnectedTick() {
  if (rtcWifiStep == 0) {
    return;
  }
  const uint32_t now = millis();
  if (wifiConnectedSince == 0) {
    wifiConnectedSince = (now != 0) ? now : 1;
    return;
  }
  if (now - wifiConnectedSince >= WIFI_STABLE_MS) {
    printUptime();
    Serial.println("A Wi-Fi 30 perce stabil - a Wi-Fi lepcso nullazva.");
    wifiLadderClear();
  }
}

// Ennyi ms múlva jöhet a következő Wi-Fi miatti router reset (0 = most).
uint32_t wifiResetWaitMs() {
  if (rtcWifiStep == 1) {
    const uint32_t kieses = wifiOutageMinutes() * 60000;
    return (quietHours() && kieses < QUIET_WINDOW_MS) ? QUIET_WINDOW_MS - kieses : 0;
  }
  if (rtcWifiStep < 2) {
    return 0;
  }
  const uint32_t varni = backoffMs((uint8_t)(rtcWifiStep - 1));
  const uint32_t eltelt = millis() - wifiLastResetAt;
  return (eltelt < varni) ? varni - eltelt : 0;
}

// Kell-e még várni a következő Wi-Fi miatti router resettel.
bool wifiResetPending() {
  return wifiResetWaitMs() > 0;
}

// A Wi-Fi próbák után: rossz jelszó -> AP; különben a lépcső következő foka, 48 óra után AP.
void wifiFailed(bool coldStart) {
  printUptime();
  Serial.print("WiFi status a probalkozasok utan: ");
  Serial.println((int)WiFi.status());
  if (wifiAuthFailed()) {
    Serial.println("Hitelesitesi hiba - valoszinuleg rossz a jelszo. AP mod.");
    wifiLadderClear();
    logEvent(EV_AP_MODE, 2);
    startConfigPortal();
    return;
  }
  wifiConnectedSince = 0;
  if (rtcWifiStep > 0 && wifiOutageMinutes() >= WIFI_GIVEUP_MIN) {
    Serial.println("Ket napja nincs Wi-Fi - AP beallito mod.");
    wifiLadderClear();
    logEvent(EV_AP_MODE, 3);
    startConfigPortal();
    return;
  }
  if (rtcWifiStep == 0) {
    wifiOutageSince = millis();
    rtcWifiOutageMin = 0;
  }
  if (rtcWifiStep == 0 && !coldStart) {
    rtcWifiStep = 1;
    wifiLadderPersist();
    Serial.println("A Wi-Fi nem jott vissza - elobb az ESP indul ujra (a router erintetlen).");
    logEvent(EV_WIFI_RESTART, (uint16_t)WiFi.status());
    saveEventLog("Wi-Fi miatti ujraindulas elott");
    lockConfigBeforeShutdown();
    Serial.flush();
    ESP.restart();
    return;
  }
  const uint32_t varni = wifiResetWaitMs();
  if (varni > 0) {
    Serial.print("A kovetkezo Wi-Fi miatti router reset ");
    Serial.print((varni + 59999) / 60000);
    Serial.println(rtcWifiStep == 1 ? " perc mulva (ejszakai csendes idoszak)." : " perc mulva.");
    currentState = WAIT_STATE;
    return;
  }
  rtcWifiStep = (rtcWifiStep < 2) ? 2 : (uint8_t)(rtcWifiStep < 255 ? rtcWifiStep + 1 : 255);
  wifiLastResetAt = millis();
  Serial.print("WiFi nem jott vissza - router ujrainditas (Wi-Fi lepcso ");
  Serial.print(rtcWifiStep);
  Serial.println(").");
  startRouterReset(DIAG_ROUTER);
}

void apSleep() {
  printUptime();
  Serial.println("Az AP beallito modban nem tortent mentes, az ESP elalszik.");
  Serial.println("Idozitett ebresztes NINCS - reset gomb vagy aramtalanitas kell.");
  logEvent(EV_SLEEP, 3);
  enterDeepSleep(0);
}

void fatalSleep() {
  printUptime();
  Serial.print(FATAL_SLEEP_AFTER_MS / 60000);
  Serial.println(" perc hibajelzes utan az ESP elalszik.");
  Serial.println("Idozitett ebresztes NINCS - reset gomb vagy aramtalanitas kell.");
  logEvent(EV_SLEEP, 4);
  enterDeepSleep(0);
}

// Végzetes hiba blokkoló környezetből: helyben jelez, 5 perc után alszik, nem tér vissza.
void fatalHalt(const char* reason) {
  enterFatal(reason);

  Serial.println("FIGYELEM: itt a wifireset gomb NEM hat, csak a reset gomb.");

  while (millis() - timing.fatalStart < FATAL_SLEEP_AFTER_MS) {
    const uint32_t now = millis();
    if (now - timing.blinkLast >= FATAL_BLINK_MS) {
      timing.blinkLast = now;
      uiFlags.blinkOn = !uiFlags.blinkOn;
      digitalWrite(ledPin, uiFlags.blinkOn ? HIGH : LOW);
      digitalWrite(wifiledPin, uiFlags.blinkOn ? HIGH : LOW);
    }

    resetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  fatalSleep();
}

// Gombos újraindítás: csak a konfigzár megszerzése után (mentés nem szakadhat félbe).
void restartFromButton(const char* reason) {
  if (!beginConfigWrite()) {
    return;
  }
  wifiLadderClear();
  Serial.println(reason);
  Serial.println("RESTART ESP32C3 device.");
  Serial.flush();
  ESP.restart();
}

void resetbutton() {
  if (configWriteInProgress()) {
    return;
  }

  if (btnResetLatched) {
    timing.resetBtnDownSince = 0;
    restartFromButton("Reset button pressed (latched).");

    return;
  }

  if (digitalRead(resetPin) != LOW) {
    timing.resetBtnDownSince = 0;
    return;
  }
  const uint32_t now = millis();
  if (timing.resetBtnDownSince == 0) {
    timing.resetBtnDownSince = (now != 0) ? now : 1;
    return;
  }

  if (now - timing.resetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    restartFromButton("Reset button pressed.");
  }
}

// A mentett Wi-Fi adatok törlése (SSID először), majd újraindítás.
void doWifiReset() {
  if (!beginConfigWrite()) {
    return;
  }
  Serial.println("WIFIRESET button is pulling down!");
  Serial.println("RESET saved wifi data!");

  bool cleared = true;
  cleared &= clearConfigValue(LittleFS, ssidPath);
  cleared &= clearConfigValue(LittleFS, passPath);
  cleared &= clearConfigValue(LittleFS, ipPath);
  cleared &= clearConfigValue(LittleFS, gatewayPath);
  if (!cleared) {
    Serial.println("!!! A mentett wifi adatok torlese NEM sikerult !!!");
    logEvent(EV_FATAL, 4);

    endConfigWrite();
    fatalHalt("A mentett wifi adatok nem torolhetok - serult fajlrendszer.");
  }
  wifiLadderClear();
  Serial.println("RESTART ESP32C3 device.");
  Serial.flush();
  ESP.restart();
}

void wifiresetbutton() {
  if (configWriteInProgress()) {
    return;
  }

  if (btnWifiResetLatched) {
    timing.wifiResetBtnDownSince = 0;
    doWifiReset();
    return;
  }
  if (digitalRead(wifiresetPin) != LOW) {
    timing.wifiResetBtnDownSince = 0;
    return;
  }
  const uint32_t now = millis();
  if (timing.wifiResetBtnDownSince == 0) {
    timing.wifiResetBtnDownSince = (now != 0) ? now : 1;
    return;
  }
  if (now - timing.wifiResetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    doWifiReset();
  }
}

// 3 csatlakozási próba 30 mp szünetekkel.
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
      waitWithButtons(wifiInterval);
    }
  }

  printUptime();
  Serial.print("WIFI FAILED TO RECONNECT AFTER ");
  Serial.print(wifi_maxRetries);
  Serial.println(" attempts!");

  return false;
}

// Csak statikus IP-nél: a beállított gateway pingelhetősége (rossz cím -> AP mód).
bool gatewayUnreachable() {
  if (!staticConfigActive) {
    return false;
  }
  const IPAddress gw = WiFi.gatewayIP();
  return (uint32_t)gw == 0 || !testInternetPing(gw, "sajat gateway");
}

// Válaszol-e a router (a gateway) pingre; cím nélkül nem.
bool gatewayPingOk() {
  const IPAddress gw = WiFi.gatewayIP();
  return (uint32_t)gw != 0 && testInternetPing(gw, "router");
}

// DHCP mellett tartalék DNS (1.1.1.1) a fallback helyen; a router DNS-e marad az első.
void ensureFallbackDns() {
  if (staticConfigActive) {
    return;
  }
  WiFi.STA.dnsIP(2, IPAddress(1, 1, 1, 1));
}

// Indulás utáni türelmi idő: a router lassabban bootol, mint az ESP.
void handleFirstStart(uint32_t currentMillis) {
  const bool online = onlineProbeDue(firstStartProbeLast, currentMillis);

  if (!online) {
    if (currentMillis - timing.startMillis < firstStartDelay) {
      if (!uiFlags.firstStartPrinted) {
        printUptime();
        Serial.println("First start wait begin.");
        uiFlags.firstStartPrinted = true;
      }
      resetbutton();
      wifiresetbutton();
      feedWatchdog();
      delay(BUTTON_POLL_MS);
      return;
    }

    printUptime();
    Serial.println("First start wait end.");

    if (!reconnectWifi()) {
      uiFlags.firstStart = false;
      wifiFailed(true);
      return;
    }
  } else {
    printUptime();
    Serial.println("First start wait end (halozat es internet visszajott).");
    digitalWrite(wifiledPin, HIGH);
  }

  uiFlags.firstStart = false;
}

void startConfigPortal() {
  if (deviceMode == MODE_CONFIG) {
    return;
  }

  saveEventLog("AP modba valtas elott");
  deviceMode = MODE_CONFIG;
  touchApDeadline();

  digitalWrite(wifiledPin, LOW);
  digitalWrite(ledPin, HIGH);
  timing.blinkLast = millis();
  uiFlags.blinkOn = false;

  startWebPortal();
}

void setup() {
  timing.startMillis = millis();

  firstStartProbeLast = timing.startMillis - CHECK_INTERVAL_MS;

  heapLogLast = timing.startMillis - HEAP_LOG_INTERVAL_MS;

  pinMode(wifiresetPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(wifiledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(wifiledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(ledPin, HIGH);

  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)relayPin);

  Serial.begin(115200);
  const uint32_t serialTimeout = millis();
  // WDT-OK: a watchdog itt még nem él, a ciklus 3 mp-re korlátos.

  while (!Serial && millis() - serialTimeout < 3000) { delay(BUTTON_POLL_MS); }
  blockingDelay(500);

  printUptime();

  initWatchdog();

  int stuckButton = pressedButtonNow();
  if (stuckButton >= 0) {
    Serial.println("Gomb lenyomva indulaskor - megvarjuk, elengedik-e.");

    bool elengedtek = false;
    const uint32_t vart = millis();
    while (millis() - vart < STUCK_BUTTON_CONFIRM_MS) {
      if (pressedButtonNow() < 0) {
        elengedtek = true;
        break;
      }
      feedWatchdog();
      delay(BUTTON_POLL_MS);
    }
    if (elengedtek) {
      stuckButton = -1;
      Serial.println("Elengedtek - ez ebreszto gombnyomas volt, indulunk tovabb.");
    }
  }
  const bool stuckRepeat =
    stuckButton >= 0 && stuckCycleAlreadyLogged((uint16_t)stuckButton);
  if (!stuckRepeat) {
    logEvent(EV_BOOT, (uint16_t)esp_reset_reason());
  }
  if (stuckButton == 0) {
    handleStuckButton("Reset button got stuck.", 0, !stuckRepeat);
  }
  if (stuckButton == 1) {
    handleStuckButton("Wifireset button got stuck.", 1, !stuckRepeat);
  }

  armButtonLatches();

  checkWatchdogResets();
  initHeapState();
  applyTimezone();
  applyHeapCarry();
  initWifiLadder();
  // Saját Wi-Fi miatti újraindulás után a router nem kapcsolt le: nincs mire várni.
  if (rtcWifiStep == 1) {
    uiFlags.firstStart = false;
  }

  Serial.println("Init LittleFS.");
  initLittleFS();

  if (!filesystemReady()) {
    if (deviceMode != MODE_FATAL) {
      logEvent(EV_FATAL, 1);
      enterFatal("A LittleFS nem csatolhato, a wifi konfiguracio nem toltheto be.");
    }
  } else {
    ConfigStatus st = CONFIG_OK;
    if (readConfigValue(LittleFS, ssidPath, ssid, sizeof(ssid)) == CONFIG_ERROR) st = CONFIG_ERROR;

    {
      char passRaw[SECRET_ENC_MAX + 1];

      if (readSecretValue(LittleFS, passPath, passRaw, sizeof(passRaw)) == CONFIG_ERROR) {
        st = CONFIG_ERROR;
        pass[0] = '\0';
      } else {
        decodeSecretInPlace(passRaw);
        strlcpy(pass, passRaw, sizeof(pass));
      }
    }
    if (readConfigValue(LittleFS, ipPath, ipStr, sizeof(ipStr)) == CONFIG_ERROR) st = CONFIG_ERROR;
    if (readConfigValue(LittleFS, gatewayPath, gatewayStr, sizeof(gatewayStr)) == CONFIG_ERROR) st = CONFIG_ERROR;

    if (st == CONFIG_ERROR && deviceMode != MODE_FATAL) {
      logEvent(EV_FATAL, 2);
      enterFatal("A mentett wifi konfiguracio nem olvashato (serult fajlrendszer).");
    }
  }

  Serial.println(ssid);
  Serial.print((unsigned)strlen(pass));
  Serial.println(" chars password loaded");

  announceApPassword();
  Serial.println(ipStr);
  Serial.println(gatewayStr);

  WiFi.persistent(false);

  if (deviceMode != MODE_FATAL) {
    if (initWiFi()) {
      Serial.println("WIFI OK!");
      logEvent(EV_WIFI_OK, (uint16_t)rtcWifiStep);
      wifiConnectedTick();
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, HIGH);
    } else if (ssid[0] == '\0') {
      logEvent(EV_AP_MODE, 1);
      startConfigPortal();
    } else {
      printUptime();
      Serial.println(uiFlags.firstStart ? "Nem sikerult csatlakozni - first start varakozas kovetkezik."
                                        : "Nem sikerult csatlakozni - ujraprobalkozas, utana router reset.");
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, LOW);
    }
  }
}

// 1 óra hibátlan futás után a watchdog- és heap-számlálók nullázódnak.
void clearHealthyRunCounters(uint32_t currentMillis) {
  if (rtcWdtResets != 0 && currentMillis - timing.startMillis >= WDT_COUNTER_CLEAR_MS) {
    rtcWdtResets = 0;
    printUptime();
    Serial.print(WDT_COUNTER_CLEAR_MS / 60000);
    Serial.println(" perc hibatlan mukodes - a watchdog szamlalo nullazva.");
  }

  if (rtcHeapRestarts != 0 && currentMillis - timing.startMillis >= WDT_COUNTER_CLEAR_MS) {
    rtcHeapRestarts = 0;
    printUptime();
    Serial.print(WDT_COUNTER_CLEAR_MS / 60000);
    Serial.println(" perc hibatlan mukodes - a heap ujrainditas szamlalo nullazva.");
  }
}

void handleFatalMode(uint32_t currentMillis) {
  if (currentMillis - timing.blinkLast >= FATAL_BLINK_MS) {
    timing.blinkLast = currentMillis;
    uiFlags.blinkOn = !uiFlags.blinkOn;
    digitalWrite(ledPin, uiFlags.blinkOn ? HIGH : LOW);
    digitalWrite(wifiledPin, uiFlags.blinkOn ? HIGH : LOW);
  }

  if (currentMillis - timing.fatalStart >= FATAL_SLEEP_AFTER_MS) {
    fatalSleep();
  }

  resetbutton();
  wifiresetbutton();
  delay(BUTTON_POLL_MS);
}

void handleConfigMode(uint32_t currentMillis) {
  if (currentMillis - timing.blinkLast >= AP_BLINK_MS) {
    timing.blinkLast = currentMillis;
    uiFlags.blinkOn = !uiFlags.blinkOn;
    digitalWrite(wifiledPin, uiFlags.blinkOn ? HIGH : LOW);
  }

  resetbutton();
  wifiresetbutton();

  if (!configWriteInProgress() && !restartRequested() &&
      (int32_t)(currentMillis - apDeadline) >= 0) {
    apSleep();
  }
  delay(BUTTON_POLL_MS);
}

// Router reset indítása (blokkol a 90 mp-es pulzus végéig), utána várakozás a bootra.
void startRouterReset(uint8_t diag) {
  logEvent(EV_NET_DIAG, diag);
  if (diag == DIAG_ROUTER && staticConfigActive && WiFi.status() == WL_CONNECTED) {
    printUptime();
    Serial.println("A sajat gateway sem elerheto - lehet, hogy rossz a statikus IP.");
    logEvent(EV_GW_UNREACHABLE, 1);
  }
  printUptime();
  Serial.println("Router reset indul.");
  while (!reset_device()) {
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  printUptime();
  Serial.print("Router reset kesz, varakozas a bootolasra (legfeljebb ");
  Serial.print(RESET_DELAY / 60000);
  Serial.println(" perc).");
  currentState = RESET_STATE;
  timing.stateStart = millis();
  resetDelayProbeLast = timing.stateStart;
}

// Csendes időszak: 22:00-05:00 helyi idő szerint; érvényes idő nélkül (NTP előtt) nem az.
bool quietHours() {
  const int ora = localHour();
  return ora >= 0 && (ora >= QUIET_START_HOUR || ora < QUIET_END_HOUR);
}

// A lépcső: az n. sikertelen reset után ennyit várunk a következővel (n >= 1).
uint32_t backoffMs(uint8_t resetek) {
  const uint8_t lepcso = (resetek <= RETRY_BACKOFF_STEPS) ? (uint8_t)(resetek - 1)
                                                          : (uint8_t)(RETRY_BACKOFF_STEPS - 1);
  return RETRY_BACKOFF_MS[lepcso];
}

// A kiesés kezdete (első reset előtt) vagy az utolsó reset óta ennyit várunk a resettel.
uint32_t resetWaitMs(bool routerOk) {
  if (testState.resetEvents == 0) {
    if (routerOk) {
      return WAN_DOWN_WINDOW_MS;
    }
    return quietHours() ? QUIET_WINDOW_MS : ROUTER_DOWN_WINDOW_MS;
  }
  return backoffMs(testState.resetEvents);
}

// Él a ping: hallgat-e tartósan a router DNS-e, miközben egy külső DNS válaszol? Igaz, ha reset indult.
bool routerDnsCheck() {
  const uint32_t now = millis();
  const uint32_t utem = dnsState.suspect ? CHECK_INTERVAL_MS : DNS_CHECK_INTERVAL_MS;
  if (dnsState.checked && now - dnsState.lastCheck < utem) {
    return false;
  }
  dnsState.checked = true;
  dnsState.lastCheck = now;

  const IPAddress dns = WiFi.dnsIP(0);
  const IPAddress gw = WiFi.gatewayIP();
  if ((uint32_t)gw == 0 || dns != gw) {
    if (!dnsState.skipLogged) {
      printUptime();
      Serial.println("A DNS szerver nem a router - a router DNS ellenorzese kimarad.");
      dnsState.skipLogged = true;
    }
    dnsState.suspect = false;
    return false;
  }
  if (testDnsServer(dns, DNS_PROBE_NAME, "router")) {
    if (dnsState.suspect) {
      printUptime();
      Serial.println("A router DNS-e ujra valaszol.");
    }
    dnsState.suspect = false;
    dnsState.resetEvents = 0;
    return false;
  }
  if (!testDnsServer(IPAddress(1, 1, 1, 1), DNS_PROBE_NAME, "kulso")) {
    printUptime();
    Serial.println("A kulso DNS sem valaszol - nem a router hibaja, nincs reset.");
    dnsState.suspect = false;
    return false;
  }
  if (!dnsState.suspect) {
    dnsState.suspect = true;
    dnsState.suspectSince = now;
  }
  const uint32_t kezdet = (dnsState.resetEvents == 0) ? dnsState.suspectSince
                                                       : dnsState.lastResetAt;
  const uint32_t varni = (dnsState.resetEvents == 0) ? DNS_FAIL_WINDOW_MS
                                                      : backoffMs(dnsState.resetEvents);
  const uint32_t eltelt = now - kezdet;
  printUptime();
  Serial.print("A router DNS-e hallgat ");
  Serial.print((now - dnsState.suspectSince) / 60000);
  Serial.println(" perce, a kulso DNS valaszol.");
  if (eltelt < varni) {
    Serial.print("Router reset legkorabban ");
    Serial.print((varni - eltelt + 59999) / 60000);
    Serial.println(" perc mulva.");
    return false;
  }
  if (dnsState.resetEvents < 255) {
    dnsState.resetEvents++;
  }
  dnsState.lastResetAt = now;
  startRouterReset(DIAG_DNS);
  return true;
}

// Percenkénti ellenőrzés: van-e internet, és ha egy ideje nincs, kell-e reset.
void runCheckState() {
  timing.stateStart = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (!lastEventWas((uint8_t)EV_WIFI_LOST)) {
      printUptime();
      logEvent(EV_WIFI_LOST, (uint16_t)WiFi.status());
      Serial.println("WiFi disconnected before test!");
    }
    digitalWrite(wifiledPin, LOW);

    // A lépcső várakozása alatt percenként egy próba, különben a szokásos 3.
    const bool siker = wifiResetPending() ? initWiFi() : reconnectWifi();
    if (siker) {
      digitalWrite(wifiledPin, HIGH);
      timing.stateStart = millis();
      return;
    }
    if (!testState.outage) {
      testState.outage = true;
      testState.outageStart = timing.stateStart;
    }
    wifiFailed(false);
    return;
  }

  digitalWrite(wifiledPin, HIGH);
  wifiConnectedTick();
  printUptime();
  Serial.println("Internet ellenorzes.");

  if (internetOk()) {
    if (testState.outage) {
      printUptime();
      Serial.print("Az internet visszajott, kieses: ");
      Serial.print((millis() - testState.outageStart) / 60000);
      Serial.println(" perc.");
    }
    testState.outage = false;
    testState.resetEvents = 0;
    Serial.println("Successful Test");
    if (!routerDnsCheck()) {
      currentState = WAIT_STATE;
    }
    return;
  }

  if (!testState.outage) {
    testState.outage = true;
    testState.outageStart = timing.stateStart;
    logEvent(EV_TEST_FAIL, (uint16_t)(testState.endpointIndex == 0 ? ENDPOINT_COUNT
                                                                    : testState.endpointIndex));
    logEvent(EV_HTTP_ERROR, httpCodeLogParam(lastHttpCode()));
  }

  const bool routerOk = gatewayPingOk();
  const uint32_t kezdet = (testState.resetEvents == 0) ? testState.outageStart
                                                        : testState.lastResetAt;
  const uint32_t eltelt = timing.stateStart - kezdet;
  const uint32_t varni = resetWaitMs(routerOk);

  printUptime();
  Serial.print("Nincs internet ");
  Serial.print((timing.stateStart - testState.outageStart) / 60000);
  Serial.print(" perce, a router ");
  Serial.println(routerOk ? "valaszol." : "sem valaszol.");

  if (eltelt >= varni) {
    startRouterReset(routerOk ? DIAG_WAN : DIAG_ROUTER);
    return;
  }
  Serial.print("Router reset legkorabban ");
  Serial.print((varni - eltelt + 59999) / 60000);
  Serial.println(" perc mulva.");
  currentState = WAIT_STATE;
}

// Reset után: várakozás a router bootjára, majd újracsatlakozás és vissza az ellenőrzéshez.
void runResetState(uint32_t currentMillis) {
  const bool earlyOnline = onlineProbeDue(resetDelayProbeLast, currentMillis);
  if (!earlyOnline && millis() - timing.stateStart < RESET_DELAY) {
    return;
  }
  printUptime();
  if (earlyOnline) {
    Serial.println("RESET_DELAY korai vege: halozat es internet OK.");
  } else {
    Serial.println("RESET_DELAY vege - ujracsatlakozas.");
    WiFi.disconnect(true);
    blockingDelay(100);

    if (!reconnectWifi()) {
      printUptime();
      Serial.println("A router reset utan sem jott vissza a WiFi.");
      digitalWrite(wifiledPin, LOW);
      wifiFailed(false);
      return;
    }
  }
  digitalWrite(wifiledPin, HIGH);

  if (gatewayUnreachable()) {
    printUptime();
    Serial.println("A router ujrainditasa utan sem elerheto a gateway.");
    Serial.println("Valoszinuleg rossz a statikus IP - AP beallito mod.");
    logEvent(EV_GW_UNREACHABLE, 2);
    logEvent(EV_AP_MODE, 4);
    digitalWrite(wifiledPin, LOW);
    startConfigPortal();
    return;
  }
  currentState = CHECK_STATE;
}

void runMonitorStateMachine(uint32_t currentMillis) {
  switch (currentState) {
    case CHECK_STATE: runCheckState(); break;
    case WAIT_STATE:
      if (currentMillis - timing.stateStart >= CHECK_INTERVAL_MS) {
        currentState = CHECK_STATE;
      }
      break;
    case RESET_STATE: runResetState(currentMillis); break;
  }

  delay(BUTTON_POLL_MS);
}

void loop() {
  const uint32_t currentMillis = millis();

  serialCommands();

  if (restartRequestDue(currentMillis)) {
    lockConfigBeforeShutdown();
    clearRestartRequest();
    wifiLadderClear();
    Serial.println("RESTART!");
    Serial.flush();
    ESP.restart();
  }

  clearHealthyRunCounters(currentMillis);

  checkHeap(currentMillis);

  ensureNtp();

  if (deviceMode == MODE_FATAL) {
    handleFatalMode(currentMillis);
    return;
  }

  if (deviceMode == MODE_CONFIG) {
    handleConfigMode(currentMillis);
    return;
  }

  if (uiFlags.firstStart) {
    handleFirstStart(currentMillis);
    return;
  }

  resetbutton();
  wifiresetbutton();
  runMonitorStateMachine(currentMillis);
}
