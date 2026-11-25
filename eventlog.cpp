#include "eventlog.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "app_hooks.h"
#include "configstore.h"
#include "sync.h"

RTC_NOINIT_ATTR uint32_t rtcEvMagic;
RTC_NOINIT_ATTR uint32_t rtcEvNext;
RTC_NOINIT_ATTR EventEntry rtcEvents[EVLOG_SIZE];
RTC_NOINIT_ATTR uint32_t rtcSavedEvNext;

static uint32_t nowEpoch() {
  const time_t t = time(nullptr);
  if (t < (time_t)NTP_MIN_VALID_EPOCH) {
    return 0;
  }
  return (uint32_t)t;
}

static bool ntpStarted = false;

static bool ntpAnnounced = false;

void ntpResetForColdBoot() {
  ntpStarted = false;
  ntpAnnounced = false;
}

static void startNtp() {
  configTzTime(NTP_TZ, NTP_SERVER);

  if (ntpAnnounced) {
    return;
  }
  ntpAnnounced = true;
  printUptime();
  Serial.print("NTP inditva: ");
  Serial.println(NTP_SERVER);
}

// Az SNTP indítása minden új kapcsolaton (a loop()-ból, minden kapcsolati útra).
void ensureNtp() {
  if (WiFi.status() != WL_CONNECTED) {
    ntpStarted = false;
    return;
  }
  if (ntpStarted) {
    return;
  }
  ntpStarted = true;
  startNtp();
}

bool formatEpoch(uint32_t epoch, char* out, size_t outSize) {
  if (epoch < NTP_MIN_VALID_EPOCH || outSize < 20) {
    return false;
  }
  const time_t t = (time_t)epoch;
  struct tm tmv;

  localtime_r(&t, &tmv);
  strftime(out, outSize, "%Y-%m-%d %H:%M:%S", &tmv);
  return true;
}

// Két task is hív: a slot írása kritikus szakaszban.
void logEvent(EventCode code, uint16_t param) {
  const uint32_t uptimeSec = (uint32_t)(esp_timer_get_time() / 1000000);

  const uint32_t epoch = nowEpoch();

  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic != EVLOG_MAGIC) {
    rtcEvMagic = EVLOG_MAGIC;
    rtcEvNext = 0;
    rtcSavedEvNext = 0;
    memset(rtcEvents, 0, sizeof(rtcEvents));
  }
  EventEntry& e = rtcEvents[rtcEvNext % EVLOG_SIZE];
  e.uptimeSec = uptimeSec;
  e.epoch = epoch;
  e.code = (uint8_t)code;
  e.param = param;
  e.reserved = 0;
  rtcEvNext++;
  portEXIT_CRITICAL(&evLogMux);
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:    return "bekapcsolas / aramtalanitas";
    case ESP_RST_EXT:        return "kulso reset lab";
    case ESP_RST_SW:         return "szoftveres ujrainditas (ESP.restart)";
    case ESP_RST_PANIC:      return "PANIC - a program osszeomlott";
    case ESP_RST_INT_WDT:    return "megszakitas-watchdog";
    case ESP_RST_TASK_WDT:   return "TASK WATCHDOG - a loop megallt";
    case ESP_RST_WDT:        return "egyeb watchdog";
    case ESP_RST_DEEPSLEEP:  return "ebredes deep sleepbol";
    case ESP_RST_BROWNOUT:   return "BROWNOUT - leesett a tapfeszultseg";
    case ESP_RST_CPU_LOCKUP: return "CPU lefagyas";

    case ESP_RST_SDIO:       return "SDIO reset";
    case ESP_RST_USB:        return "USB reset (feltoltes vagy soros monitor)";
    case ESP_RST_JTAG:       return "JTAG reset (hibakereso)";
    case ESP_RST_EFUSE:      return "eFuse hiba";
    case ESP_RST_PWR_GLITCH: return "TAPFESZULTSEG-TUSKE";
    default:                 return "ismeretlen";
  }
}

const char* eventName(uint8_t code) {
  switch (code) {
    case EV_BOOT: return "BOOT";
    case EV_WIFI_OK: return "WIFI OK";
    case EV_WIFI_LOST: return "WIFI LOST";
    case EV_TEST_FAIL: return "TEST FAIL";
    case EV_ROUTER_RESET: return "ROUTER RESET";
    case EV_AP_MODE: return "AP MODE";
    case EV_CONFIG_SAVED: return "CONFIG SAVED";
    case EV_SLEEP: return "SLEEP";
    case EV_FATAL: return "FATAL";
    case EV_WDT_RESET: return "WDT RESET";
    case EV_STUCK_BUTTON: return "STUCK BUTTON";
    case EV_GW_UNREACHABLE: return "GW UNREACH";
    case EV_LOW_HEAP: return "LOW HEAP";
    case EV_HEAP_RESTART: return "HEAP RESTART";
    case EV_HTTP_ERROR: return "HTTP ERROR";
    case EV_NET_DIAG: return "NET DIAG";
    default: return "?";
  }
}

bool lastEventWas(uint8_t code) {
  bool egyezik = false;
  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic == EVLOG_MAGIC && rtcEvNext > 0) {
    egyezik = (rtcEvents[(rtcEvNext - 1) % EVLOG_SIZE].code == code);
  }
  portEXIT_CRITICAL(&evLogMux);
  return egyezik;
}

bool stuckCycleAlreadyLogged(uint16_t which) {
  bool egyezik = false;
  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic == EVLOG_MAGIC && rtcEvNext >= 2) {
    const EventEntry& last = rtcEvents[(rtcEvNext - 1) % EVLOG_SIZE];
    const EventEntry& prev = rtcEvents[(rtcEvNext - 2) % EVLOG_SIZE];
    egyezik = last.code == EV_STUCK_BUTTON && last.param == which
              && prev.code == EV_BOOT;
  }
  portEXIT_CRITICAL(&evLogMux);
  return egyezik;
}

bool loadEventLogRange(const EvFileHeader& fej, uint32_t tol, uint16_t db,
                       EventEntry* ki) {
  if (fej.count == 0 || fej.count > EVFILE_SIZE) {
    return false;
  }
  if (db == 0 || tol > fej.count || (uint32_t)db > (uint32_t)fej.count - tol) {
    return false;
  }
  if (!filesystemReady() || !LittleFS.exists(evLogPath)) {
    return false;
  }
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }

  EventEntry szemet;
  EvFileHeader eldobando;
  if (f.read((uint8_t*)&eldobando, sizeof(eldobando)) != sizeof(eldobando)) {
    f.close();
    return false;
  }
  for (uint32_t i = 0; i < tol; i++) {
    if (f.read((uint8_t*)&szemet, sizeof(szemet)) != sizeof(szemet)) {
      f.close();
      return false;
    }
  }
  const size_t kell = sizeof(EventEntry) * db;
  const size_t olvasott = f.read((uint8_t*)ki, kell);
  f.close();
  return olvasott == kell;
}

static void printOneEntry(uint16_t sorszam, const EventEntry& e) {
  char ido[24];
  if (e.epoch >= NTP_MIN_VALID_EPOCH && formatEpoch(e.epoch, ido, sizeof(ido))) {
    Serial.print(ido);
  } else {
    Serial.print("        -          ");
  }
  Serial.printf("  %3u  %3u:%02u:%02u  %-14s %u\n",
                (unsigned)sorszam,
                (unsigned)(e.uptimeSec / 3600), (unsigned)((e.uptimeSec % 3600) / 60),
                (unsigned)(e.uptimeSec % 60), eventName(e.code), (unsigned)e.param);
}

void printEventLogs() {
  Serial.println();
  Serial.println("=== ESEMENYNAPLO ===");
  Serial.println("valos ido            sor   uptime    esemeny        param");

  uint16_t sorszam = 0;
  EvFileHeader fej;
  const bool vanFajl = loadEventLogHeader(fej);
  if (vanFajl) {
    Serial.printf("-- fajl (%s): %u bejegyzes\n", evLogPath, (unsigned)fej.count);
    EventEntry darab[8];
    uint32_t tol = 0;
    while (tol < fej.count) {
      const uint16_t db = (uint16_t)((fej.count - tol < 8) ? (fej.count - tol) : 8);
      if (!loadEventLogRange(fej, tol, db, darab)) {
        Serial.println("   (a fajl olvasasa megszakadt)");
        break;
      }
      for (uint16_t i = 0; i < db; i++) {
        printOneEntry(++sorszam, darab[i]);
      }
      tol += db;
    }
  } else {
    Serial.println("-- fajl: nincs, vagy nem olvashato");
  }

  uint32_t evTotal, mentettEddig;
  EventEntry masolat[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  mentettEddig = rtcSavedEvNext;
  memcpy(masolat, rtcEvents, sizeof(masolat));
  portEXIT_CRITICAL(&evLogMux);

  const uint32_t alap = vanFajl
      ? (mentettEddig < fej.evNextAtSave ? mentettEddig : fej.evNextAtSave) : 0;
  const uint32_t keletkezett = (evTotal > alap) ? (evTotal - alap) : 0;
  const uint16_t ujDb = (uint16_t)(keletkezett < EVLOG_SIZE ? keletkezett : EVLOG_SIZE);
  Serial.printf("-- RTC (a mentes ota): %u bejegyzes\n", (unsigned)ujDb);
  for (uint16_t i = 0; i < ujDb; i++) {
    printOneEntry(++sorszam, masolat[(evTotal - ujDb + i) % EVLOG_SIZE]);
  }
  Serial.println("=== NAPLO VEGE ===");
  Serial.println();
}

// A fájlhoz csak az új bejegyzéseket fűzi; flash-kímélés: új esemény nélkül nem ír.
bool saveEventLog(const char* reason) {
  if (!filesystemReady()) {
    return false;
  }

  uint32_t evTotal;
  uint32_t mentettEddig;
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  mentettEddig = rtcSavedEvNext;
  portEXIT_CRITICAL(&evLogMux);
  if (evTotal == 0 || evTotal == mentettEddig) {
    return false;
  }

  if (!beginConfigWrite()) {
    printUptime();
    Serial.println("A naplo mentese kimarad: eppen mas fajliras folyik.");
    return false;
  }

  EventEntry masolat[EVLOG_SIZE];
  uint32_t evNextMost;
  portENTER_CRITICAL(&evLogMux);
  evNextMost = rtcEvNext;
  memcpy(masolat, rtcEvents, sizeof(masolat));
  portEXIT_CRITICAL(&evLogMux);

  const uint32_t keletkezett = evNextMost - mentettEddig;
  const uint16_t ujDb = (uint16_t)(keletkezett < EVLOG_SIZE ? keletkezett : EVLOG_SIZE);

  EventEntry uj[EVFILE_SIZE];
  uint16_t osszes = 0;

  EvFileHeader regi;
  if (loadEventLogHeader(regi)) {
    const uint16_t ferohely = (uint16_t)(EVFILE_SIZE - ujDb);
    const uint16_t megtart = regi.count < ferohely ? regi.count : ferohely;
    const uint32_t ettol = (uint32_t)(regi.count - megtart);
    if (megtart > 0 && !loadEventLogRange(regi, ettol, megtart, uj)) {
      Serial.println("- a korabbi naplofajl nem olvashato, csak az ujakat mentjuk");
    } else {
      osszes = megtart;
    }
  }

  for (uint16_t i = 0; i < ujDb; i++) {
    const uint32_t sorszam = evNextMost - ujDb + i;
    uj[osszes + i] = masolat[sorszam % EVLOG_SIZE];
  }
  osszes = (uint16_t)(osszes + ujDb);

  EvFileHeader fej;
  fej.magic = EVFILE_MAGIC;
  fej.version = EVFILE_VERSION;
  fej.count = osszes;
  fej.evNextAtSave = evNextMost;
  fej.savedEpoch = nowEpoch();
  fej.savedUptime = (uint32_t)(esp_timer_get_time() / 1000000);

  printUptime();
  Serial.print("Naplo mentese a fajlrendszerre (");
  Serial.print(reason);
  Serial.println(")...");

  bool ok = false;
  File f = LittleFS.open(evLogPath, FILE_WRITE);
  if (!f) {
    Serial.println("- a naplofajl nem nyithato irasra");
  } else {
    const size_t fejBytes = f.write((const uint8_t*)&fej, sizeof(fej));
    const size_t adatBytes = fej.count
        ? f.write((const uint8_t*)uj, sizeof(EventEntry) * fej.count) : 0;
    f.flush();
    f.close();
    const size_t vart = sizeof(fej) + sizeof(EventEntry) * fej.count;
    if (fejBytes + adatBytes != vart) {
      Serial.print("- rovid iras: ");
      Serial.print((unsigned)(fejBytes + adatBytes));
      Serial.print(" / ");
      Serial.print((unsigned)vart);
      Serial.println(" bajt (megtelt a fajlrendszer?)");
    } else {
      File v = LittleFS.open(evLogPath, "r");
      if (!v) {
        Serial.println("- verify: a fajl nem nyithato olvasasra");
      } else {
        EvFileHeader vissza;
        const size_t olvasott = v.read((uint8_t*)&vissza, sizeof(vissza));
        const size_t meret = v.size();
        v.close();
        if (olvasott != sizeof(vissza) || meret != vart
            || vissza.magic != EVFILE_MAGIC || vissza.count != fej.count
            || vissza.evNextAtSave != fej.evNextAtSave) {
          Serial.println("- verify FAILED: a visszaolvasott fejlec nem egyezik");
        } else {
          ok = true;
        }
      }
    }
  }

  if (ok) {
    rtcSavedEvNext = fej.evNextAtSave;
    Serial.print("- naplo mentve, ");
    Serial.print((unsigned)fej.count);
    Serial.print(" bejegyzes (ebbol ");
    Serial.print((unsigned)ujDb);
    Serial.println(" uj)");
  } else {
    Serial.println("- a naplo mentese NEM sikerult, az RTC naplo ettol meg el");
  }
  endConfigWrite();
  return ok;
}

bool loadEventLogHeader(EvFileHeader& fej) {
  if (!filesystemReady() || !LittleFS.exists(evLogPath)) {
    return false;
  }
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }
  const size_t meret = f.size();
  if (meret < sizeof(EvFileHeader)
      || f.read((uint8_t*)&fej, sizeof(fej)) != sizeof(fej)) {
    f.close();
    return false;
  }
  f.close();
  if (fej.magic != EVFILE_MAGIC || fej.version != EVFILE_VERSION
      || fej.count == 0 || fej.count > EVFILE_SIZE) {
    return false;
  }

  return meret >= sizeof(EvFileHeader) + sizeof(EventEntry) * fej.count;
}

