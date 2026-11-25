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

constexpr uint8_t ledPin = D4;

constexpr uint8_t wifiledPin = D3;

constexpr uint8_t relayPin = D10;

constexpr uint8_t wifiresetPin = D0;

constexpr uint8_t resetPin = D1;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20 * 1000;
constexpr uint32_t SUCCESS_DELAY = 1 * 60 * 1000;

constexpr uint32_t PROBE_DELAY = 12 * 1000;
constexpr uint32_t RESET_DELAY = 10 * 60 * 1000;
constexpr uint32_t RESET_PULSE = 90 * 1000;
constexpr uint32_t firstStartDelay = 10 * 60 * 1000;

constexpr uint8_t maxfailureEvents = 5;

constexpr uint8_t maxfailureEventsAfterSleep = 2;

constexpr uint8_t wifi_maxRetries = 3;
constexpr uint32_t wifiInterval = 30 * 1000;

constexpr uint32_t MAX_RETRY_ROUNDS = 33;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;

constexpr uint32_t BUTTON_POLL_MS = 10;

constexpr uint32_t FATAL_BLINK_MS = 100;

constexpr uint32_t FATAL_SLEEP_AFTER_MS = 5 * 60 * 1000;

constexpr uint32_t STUCK_BLINK_MS = 3000;

constexpr uint32_t AP_BLINK_MS = 500;

constexpr uint32_t RESET_BLINK_MS = 250;

constexpr uint32_t WDT_TIMEOUT_MS = 90 * 1000;

constexpr uint32_t MAX_WDT_RESETS = 3;

constexpr uint32_t WDT_COUNTER_CLEAR_MS = 60 * 60 * 1000;

constexpr uint64_t SLEEP_DURATION_US = 3600ULL * 1000000ULL;
constexpr uint64_t STUCK_BUTTON_SLEEP_US = 60ULL * 1000000ULL;

constexpr uint32_t STUCK_BUTTON_CONFIRM_MS = 3000;

constexpr uint32_t HEAP_CHECK_INTERVAL_MS = 10 * 1000;
constexpr uint32_t HEAP_LOG_INTERVAL_MS   = 30 * 60 * 1000;

constexpr uint32_t HEAP_WARN_BYTES  = 25000;
constexpr uint32_t HEAP_CRIT_BYTES  = 12000;

constexpr uint32_t HEAP_CRIT_BLOCK_BYTES = 6000;

constexpr uint8_t HEAP_CRIT_SAMPLES = 3;

constexpr uint32_t MAX_HEAP_RESTARTS = 3;

constexpr uint32_t ONLINE_PROBE_INTERVAL_MS = 60 * 1000;

constexpr uint8_t PROBE_PING_IP[4] = { 1, 1, 1, 1 };

constexpr uint8_t MAX_CYCLE_INDEX = 4;

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
static_assert(sizeof(TEST_ENDPOINTS) / sizeof(TEST_ENDPOINTS[0]) == MAX_CYCLE_INDEX + 1,
              "minden ciklus-indexhez PONTOSAN egy vegpont tartozzon");

constexpr uint8_t RESET_TRIGGER_CYCLE = 3;

struct TestState {
  uint8_t cycleIndex = 0;
  uint8_t failedCount = 0;
  uint8_t resetEvents = 0;
  uint8_t resetStep = 0;
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
  bool successPrinted = false;
  bool resetPrinted = false;
  bool firstStartPrinted = false;
  bool firstStart = true;
  bool blinkOn = false;
};

TestState testState;
TimingState timing;
UIFlags uiFlags;

enum State : uint8_t {
  TESTING_STATE = 0,
  FAILURE_STATE = 1,
  SUCCESS_STATE = 2
};

enum DeviceMode : uint8_t {
  MODE_MONITOR = 0,
  MODE_CONFIG = 1,
  MODE_FATAL = 2
};

State currentState = TESTING_STATE;
DeviceMode deviceMode = MODE_MONITOR;

bool staticConfigActive = false;

volatile bool btnResetLatched = false;
volatile bool btnWifiResetLatched = false;
volatile uint32_t btnResetDownAt = 0;
volatile uint32_t btnWifiResetDownAt = 0;

uint32_t resetDelayProbeLast = 0;
uint32_t firstStartProbeLast = 0;

uint32_t heapCheckLast = 0;
uint32_t heapLogLast = 0;
uint8_t  heapCritStreak = 0;
bool     heapWarnActive = false;

bool watchdogEnabled = false;

constexpr uint32_t WDT_COUNTER_MAGIC = 0x42415A53UL;
RTC_NOINIT_ATTR uint32_t rtcWdtMagic;
RTC_NOINIT_ATTR uint32_t rtcWdtResets;

RTC_DATA_ATTR uint32_t rtcRetryRounds = 0;

RTC_DATA_ATTR uint32_t rtcRouterFailRounds = 0;

enum NetDiag : uint8_t {
  DIAG_ROUTER = 1,
  DIAG_WAN = 2,
  DIAG_HTTP = 3
};

constexpr uint32_t HOLD_OFF_MS = 10 * 60 * 1000;
constexpr uint32_t CONFIRM_DELAY_MS = 3 * 60 * 1000;
constexpr uint32_t GW_LEARN_INTERVAL_MS = 60 * 60 * 1000;

RTC_DATA_ATTR bool gwAnswersPing = false;  // a router pingelhetősége: a deep sleepet túléli
uint32_t gwLearnLast = 0;
bool wanResetDone = false;
bool confirmPending = false;
bool holdOffActive = false;
uint32_t holdOffMs = HOLD_OFF_MS;
uint8_t lastHoldDiag = 0;

DiagCounters diagCounters() {
  return DiagCounters{ rtcWdtResets, MAX_WDT_RESETS, rtcRetryRounds, MAX_RETRY_ROUNDS };
}

constexpr uint32_t HEAP_MAGIC = 0x42415A48UL;
constexpr uint8_t  CARRY_NONE = 0xFF;
RTC_NOINIT_ATTR uint32_t rtcHeapMagic;
RTC_NOINIT_ATTR uint32_t rtcHeapRestarts;
RTC_NOINIT_ATTR uint8_t  rtcCarryResetEvents;

RTC_NOINIT_ATTR uint32_t rtcCarryRetryRounds;

RTC_NOINIT_ATTR uint32_t rtcCarryRouterFailRounds;

RTC_NOINIT_ATTR uint32_t rtcCarryDiagState;

void printUptime();
void resetbutton();
void wifiresetbutton();
void armButtonLatches();
void restartFromButton(const char* reason);
void doWifiReset();
void blockingDelay(uint32_t duration);
void waitWithButtons(uint32_t duration);
bool waitWithButtonsUntilOnline(uint32_t duration);
bool onlineProbe();
bool onlineProbeDue(uint32_t& lastProbe, uint32_t now);
void lockConfigBeforeShutdown();
void routerFailSleep();
void fatalSleep();
void apSleep();
void feedWatchdog();
void wifiGiveUp();
bool routerResetAndRetry();
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
uint8_t diagnoseNetwork();
void learnGatewayPing(uint32_t now);
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
  Serial.println("Reset gomb: ujraindítas. Wifireset gomb: mentett adatok torlese.");
  Serial.print(FATAL_SLEEP_AFTER_MS / 60000);
  Serial.println(" perc mulva az eszkoz elalszik (magatol nem ebred fel).");
}

void initHeapState() {
  if (rtcHeapMagic != HEAP_MAGIC) {
    rtcHeapMagic = HEAP_MAGIC;
    rtcHeapRestarts = 0;
    rtcCarryResetEvents = CARRY_NONE;
    rtcCarryRetryRounds = 0;
    rtcCarryRouterFailRounds = 0;
    rtcCarryDiagState = 0;
  }
}

void applyHeapCarry() {
  if (rtcCarryResetEvents == CARRY_NONE) {
    return;
  }
  testState.resetEvents = rtcCarryResetEvents;

  rtcRetryRounds = rtcCarryRetryRounds;
  rtcRouterFailRounds = rtcCarryRouterFailRounds;
  wanResetDone = (rtcCarryDiagState & 1u) != 0;
  gwAnswersPing = (rtcCarryDiagState & 2u) != 0;
  lastHoldDiag = (uint8_t)((rtcCarryDiagState >> 8) & 0xFFu);
  rtcCarryResetEvents = CARRY_NONE;
  printUptime();
  Serial.print("Heap miatti ujraindulas utan folytatjuk: router reset szamlalo = ");
  Serial.print(testState.resetEvents);
  Serial.print(" / ");
  Serial.print(maxfailureEvents);
  Serial.print(", ujraprobalkozasi kor = ");
  Serial.print(rtcRetryRounds);
  Serial.print(" / ");
  Serial.println(MAX_RETRY_ROUNDS);
}

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

  const bool resetEllenorzoAblak =
      (currentState == FAILURE_STATE) && uiFlags.resetPrinted;
  if (deviceMode != MODE_MONITOR || configWriteInProgress() || restartRequested()
      || testState.resetStep != 0 || resetEllenorzoAblak) {
    return;
  }

  if (rtcHeapRestarts >= MAX_HEAP_RESTARTS) {
    logEvent(EV_FATAL, 5);
    enterFatal("Tartosan keves a szabad heap - az ujrainditas sem segitett.");
    return;
  }

  rtcCarryResetEvents = testState.resetEvents;
  rtcCarryRetryRounds = rtcRetryRounds;
  rtcCarryRouterFailRounds = rtcRouterFailRounds;
  rtcCarryDiagState = (wanResetDone ? 1u : 0u) | (gwAnswersPing ? 2u : 0u)
                      | ((uint32_t)lastHoldDiag << 8);
  rtcHeapRestarts++;
  logEvent(EV_HEAP_RESTART, (uint16_t)(szabad / 1024));

  saveEventLog("heap miatti ujraindulas elott");

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

bool onlineProbe() {
  if (WiFi.status() != WL_CONNECTED) {
    if (ssid[0] != '\0') {
      WiFi.begin(ssid, pass);
    }

    return false;
  }

  const IPAddress target(PROBE_PING_IP[0], PROBE_PING_IP[1],
                         PROBE_PING_IP[2], PROBE_PING_IP[3]);
  return testInternetPing(target, "internet");
}

bool onlineProbeDue(uint32_t& lastProbe, uint32_t now) {
  if (now - lastProbe < ONLINE_PROBE_INTERVAL_MS) {
    return false;
  }
  lastProbe = now;
  return onlineProbe();
}

bool waitWithButtonsUntilOnline(uint32_t duration) {
  const uint32_t start = millis();
  uint32_t lastProbe = start;
  while (millis() - start < duration) {
    if (onlineProbeDue(lastProbe, millis())) {
      printUptime();
      Serial.println("A varakozas korabban veget er: halozat es internet OK.");
      return true;
    }
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  return false;
}

constexpr uint32_t SAVE_WAIT_MAX_MS = 5000;

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
  // WDT-OK: korlatos es rovid. A ciklus pontosan STUCK_BLINK_MS-ig (3000 ms)

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

bool reset_device() {
  if (testState.resetStep == 0) {
    testState.resetEvents++;

    const uint8_t korlat = (rtcRouterFailRounds == 0) ? maxfailureEvents
                                                        : maxfailureEventsAfterSleep;
    if (testState.resetEvents >= korlat) {
      routerFailSleep();
    }
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

void enterDeepSleep(uint64_t timerUs) {
  saveEventLog("alvas elott");

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

void routerFailSleep() {
  printUptime();
  Serial.print("A router ujrainditasa ");
  Serial.print(testState.resetEvents - 1);
  Serial.println(" alkalommal sem segitett.");
  if (rtcRouterFailRounds < 0xFFFFFFFFUL) {
    rtcRouterFailRounds++;
  }
  Serial.print("Sikertelen reset-kor: ");
  Serial.print(rtcRouterFailRounds);
  Serial.print(" - a kovetkezo korben ");
  Serial.print(maxfailureEventsAfterSleep - 1);
  Serial.println(" router reset jar, aztan ujra alvas.");
  Serial.print("Alvas ");
  Serial.print((unsigned long)(SLEEP_DURATION_US / 60000000ULL));
  Serial.println(" percre, utana automatikus ujraprobalkozas.");
  logEvent(EV_SLEEP, 2);
  enterDeepSleep(SLEEP_DURATION_US);
}

bool routerResetAndRetry() {
  printUptime();
  Serial.println("A halozat nem latszik - router ujrainditas kovetkezik.");
  while (!reset_device()) {
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    delay(BUTTON_POLL_MS);
  }
  printUptime();
  Serial.println("Router reset kesz, varakozas a bootolasra.");

  if (waitWithButtonsUntilOnline(RESET_DELAY)) {
    digitalWrite(wifiledPin, HIGH);
    return true;
  }
  return reconnectWifi();
}

bool wifiAuthFailed() {
  return WiFi.status() == WL_CONNECT_FAILED;
}

void retrySleep() {
  printUptime();
  Serial.print("A halozat nem elerheto. Ujraprobalkozasi kor: ");
  Serial.print(rtcRetryRounds);
  Serial.print(" / ");
  Serial.println(MAX_RETRY_ROUNDS);

  Serial.print("Alvas ");
  Serial.print((unsigned long)(SLEEP_DURATION_US / 60000000ULL));
  Serial.println(" percre, utana automatikus ujraprobalkozas.");
  logEvent(EV_SLEEP, 1);
  enterDeepSleep(SLEEP_DURATION_US);
}

void wifiGiveUp() {
  const wl_status_t st = WiFi.status();
  printUptime();
  Serial.print("WiFi status a probalkozasok utan: ");
  Serial.println((int)st);

  if (st == WL_CONNECT_FAILED) {
    Serial.println("Hitelesitesi hiba - valoszinuleg rossz a jelszo. AP mod.");
    logEvent(EV_AP_MODE, 2);
    startConfigPortal();
    return;
  }

  rtcRetryRounds++;
  if (rtcRetryRounds >= MAX_RETRY_ROUNDS) {
    Serial.println("Ket napja nincs halozat - AP beallito mod.");
    rtcRetryRounds = 0;
    logEvent(EV_AP_MODE, 3);
    startConfigPortal();
    return;
  }
  retrySleep();
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

void restartFromButton(const char* reason) {
  if (!beginConfigWrite()) {
    return;
  }
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
    Serial.println("!!! A mentett wifi adatok törlése NEM sikerült !!!");
    logEvent(EV_FATAL, 4);

    endConfigWrite();
    fatalHalt("A mentett wifi adatok nem torolhetok - serult fajlrendszer.");
  }
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

// Hol a hiba: a routerben, a szolgáltatónál, vagy csak a HTTP tesztekben?
uint8_t diagnoseNetwork() {
  printUptime();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Diagnozis: nincs Wi-Fi kapcsolat - router hiba.");
    return DIAG_ROUTER;
  }
  const IPAddress gw = WiFi.gatewayIP();
  if ((uint32_t)gw == 0) {
    Serial.println("Diagnozis: nincs gateway cim - router hiba.");
    return DIAG_ROUTER;
  }
  if (testInternetPing(gw, "router")) {
    gwAnswersPing = true;
  } else if (gwAnswersPing || staticConfigActive) {
    Serial.println("Diagnozis: a router nem valaszol - router hiba.");
    return DIAG_ROUTER;
  } else {
    Serial.println("Diagnozis: a router pingre nem valaszol, ezt a jelet nem hasznaljuk.");
  }
  const IPAddress inet(PROBE_PING_IP[0], PROBE_PING_IP[1], PROBE_PING_IP[2], PROBE_PING_IP[3]);
  if (!testInternetPing(inet, "internet")) {
    Serial.println("Diagnozis: a router el, az internet nem - szolgaltatoi hiba.");
    return DIAG_WAN;
  }
  Serial.println("Diagnozis: router es internet rendben - csak a HTTP tesztek buknak.");
  return DIAG_HTTP;
}

// A router pingelhetőségének megtanulása működő hálózatnál (óránként legfeljebb egyszer).
void learnGatewayPing(uint32_t now) {
  if (gwAnswersPing || now - gwLearnLast < GW_LEARN_INTERVAL_MS) {
    return;
  }
  gwLearnLast = now;
  const IPAddress gw = WiFi.gatewayIP();
  if ((uint32_t)gw != 0 && testInternetPing(gw, "router")) {
    gwAnswersPing = true;
  }
}

void ensureFallbackDns() {
  if (staticConfigActive) {
    return;
  }
  WiFi.STA.dnsIP(2, IPAddress(1, 1, 1, 1));
}

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
      if (wifiAuthFailed()) {
        wifiGiveUp();
        return;
      }

      if (!routerResetAndRetry()) {
        wifiGiveUp();
        return;
      }
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

  firstStartProbeLast = timing.startMillis - ONLINE_PROBE_INTERVAL_MS;
  gwLearnLast = timing.startMillis - GW_LEARN_INTERVAL_MS;

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
  // WDT-OK: itt a watchdog MEG NEM EL - az initWatchdog() csak nehany sorral

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
  applyHeapCarry();

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
      logEvent(EV_WIFI_OK, (uint16_t)rtcRetryRounds);
      rtcRetryRounds = 0;
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, HIGH);
    } else if (ssid[0] == '\0') {
      logEvent(EV_AP_MODE, 1);
      startConfigPortal();
    } else {
      printUptime();
      Serial.println("Nem sikerult csatlakozni - first start varakozas kovetkezik.");
      deviceMode = MODE_MONITOR;
      digitalWrite(wifiledPin, LOW);
    }
  }
}

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

void runTestingState() {
  if (WiFi.status() != WL_CONNECTED) {
    const bool ismetles = lastEventWas((uint8_t)EV_WIFI_LOST);
    if (!ismetles) {
      printUptime();
      logEvent(EV_WIFI_LOST, (uint16_t)WiFi.status());
      Serial.println("WiFi disconnected before test!");
    }
    digitalWrite(wifiledPin, LOW);

    if (reconnectWifi()) {
      timing.stateStart = millis();
      return;
    }

    if (wifiAuthFailed()) {
      printUptime();
      Serial.println("WiFi hitelesitesi hiba - router reset helyett AP mod.");
      digitalWrite(wifiledPin, LOW);
      wifiGiveUp();
      return;
    }

    printUptime();
    Serial.println("WiFi nem jott vissza - router ujrainditas kovetkezik.");
    testState.cycleIndex = RESET_TRIGGER_CYCLE + 1;
    currentState = FAILURE_STATE;
    timing.stateStart = millis();
    return;
  }

  digitalWrite(wifiledPin, HIGH);
  if (rtcRetryRounds != 0) {
    rtcRetryRounds = 0;
  }
  printUptime();
  Serial.println("Beginning Test.");

  Serial.print("Teszt ciklus index = ");
  Serial.println(testState.cycleIndex + 1);

  ensureFallbackDns();

  const TestEndpoint& vegpont = TEST_ENDPOINTS[testState.cycleIndex];
  const bool testResult = testInternetHTTP(vegpont.url, vegpont.expect);

  if (testResult) {
    testState.cycleIndex = 0;
    testState.failedCount = 0;
    testState.resetEvents = 0;
    rtcRouterFailRounds = 0;
    wanResetDone = false;
    confirmPending = false;
    lastHoldDiag = 0;
    currentState = SUCCESS_STATE;
    learnGatewayPing(millis());
  } else {
    testState.failedCount++;

    if (testState.failedCount == 1 && lastHoldDiag == 0 && !confirmPending) {
      logEvent(EV_TEST_FAIL, (uint16_t)(testState.cycleIndex + 1));
      logEvent(EV_HTTP_ERROR, httpCodeLogParam(lastHttpCode()));
    }
    printUptime();

    Serial.print("Test failed. | Hibák száma = ");
    Serial.print(testState.failedCount);
    Serial.print(" / ");
    Serial.println(MAX_CYCLE_INDEX + 1);
    currentState = FAILURE_STATE;
  }

  timing.stateStart = millis();
  return;
}

void runFailureState(uint32_t currentMillis) {
  if (holdOffActive) {
    if (currentMillis - timing.stateStart >= holdOffMs) {
      printUptime();
      Serial.println("A varakozas letelt - ujra teszteljuk az internetet.");
      holdOffActive = false;
      testState.cycleIndex = 0;
      testState.failedCount = 0;
      currentState = TESTING_STATE;
      timing.stateStart = currentMillis;
    }
    return;
  }

  if (testState.cycleIndex > RESET_TRIGGER_CYCLE) {
    if (!uiFlags.resetPrinted) {
      const uint8_t diag = diagnoseNetwork();
      // A router él: csak HTTP hiba, vagy a szolgáltatói kiesés már kapott egy resetet.
      if (diag == DIAG_HTTP || (diag == DIAG_WAN && wanResetDone)) {
        confirmPending = false;
        printUptime();
        Serial.print("A router reset ELMARAD (a router el). Ujra teszt ");
        Serial.print(HOLD_OFF_MS / 60000);
        Serial.println(" perc mulva.");
        if (diag != lastHoldDiag) {
          logEvent(EV_NET_DIAG, diag);
          logEvent(EV_RESET_SKIPPED, diag);
          lastHoldDiag = diag;
        }
        holdOffActive = true;
        holdOffMs = HOLD_OFF_MS;
        timing.stateStart = millis();
        return;
      }
      // Szolgáltatói kiesés: csak akkor reset, ha a megerősítő kör után is tart.
      if (diag == DIAG_WAN && !confirmPending) {
        confirmPending = true;
        printUptime();
        Serial.print("Szolgaltatoi kieses - megerosito kor: ujra teszt ");
        Serial.print(CONFIRM_DELAY_MS / 60000);
        Serial.println(" perc mulva.");
        holdOffActive = true;
        holdOffMs = CONFIRM_DELAY_MS;
        timing.stateStart = millis();
        return;
      }
      confirmPending = false;
      lastHoldDiag = 0;
      if (diag == DIAG_WAN) {
        wanResetDone = true;
      }
      logEvent(EV_NET_DIAG, diag);
      if (diag == DIAG_ROUTER && staticConfigActive && WiFi.status() == WL_CONNECTED) {
        printUptime();
        Serial.println("A sajat gateway sem elerheto - lehet, hogy rossz a statikus IP.");
        logEvent(EV_GW_UNREACHABLE, 1);
      }
      printUptime();
      Serial.println("Beginning Reset in FAILURE_STATE.");
      while (!reset_device()) {
        resetbutton();
        wifiresetbutton();
        feedWatchdog();
        delay(BUTTON_POLL_MS);
      }
      printUptime();
      Serial.println("Reset is done in FAILURE_STATE.");
      Serial.println("RESET_DELAY start in FAILURE_STATE.");
      timing.stateStart = millis();
      uiFlags.resetPrinted = true;
      resetDelayProbeLast = millis();
      return;
    }

    const bool earlyOnline = onlineProbeDue(resetDelayProbeLast, millis());

    if (earlyOnline || millis() - timing.stateStart >= RESET_DELAY) {
      printUptime();
      if (earlyOnline) {
        Serial.println("RESET_DELAY korai vege: halozat es internet OK.");
      } else {
        Serial.println("RESET_DELAY end in FAILURE_STATE.");
      }

      if (!earlyOnline) {
        Serial.println("Reconnect WIFI in FAILURE_STATE.");
        WiFi.disconnect(true);
        blockingDelay(100);

        if (!reconnectWifi()) {
          printUptime();
          Serial.println("A router reset utan sem jott vissza a WiFi.");
          digitalWrite(wifiledPin, LOW);
          wifiGiveUp();

          return;
        }

        printUptime();
        Serial.println("WIFI OK in FAILURE_STATE.");
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

      testState.cycleIndex = 0;
      testState.failedCount = 0;
      uiFlags.resetPrinted = false;
      currentState = TESTING_STATE;
    }
  } else {
    if (currentMillis - timing.stateStart >= PROBE_DELAY) {
      if (testState.cycleIndex < MAX_CYCLE_INDEX) {
        testState.cycleIndex++;
      }
      currentState = TESTING_STATE;
    }
  }
}

void runSuccessState(uint32_t currentMillis) {
  if (!uiFlags.successPrinted) {
    printUptime();
    Serial.println("Successful Test");
    Serial.println();
    Serial.println("SUCCESS_DELAY delay start.");
    uiFlags.successPrinted = true;
  }

  if (currentMillis - timing.stateStart >= SUCCESS_DELAY) {
    printUptime();
    Serial.println("SUCCESS_DELAY delay end.");
    uiFlags.successPrinted = false;
    timing.stateStart = currentMillis;
    currentState = TESTING_STATE;
  }
}

void runMonitorStateMachine(uint32_t currentMillis) {
  switch (currentState) {
    case TESTING_STATE: runTestingState();             break;
    case FAILURE_STATE: runFailureState(currentMillis); break;
    case SUCCESS_STATE: runSuccessState(currentMillis); break;
  }

  delay(BUTTON_POLL_MS);
}

void loop() {
  const uint32_t currentMillis = millis();

  serialCommands();

  if (restartRequestDue(currentMillis)) {
    lockConfigBeforeShutdown();
    clearRestartRequest();
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
