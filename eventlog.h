#pragma once

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <stddef.h>
#include <stdint.h>

enum EventCode : uint8_t {
  EV_BOOT = 1,
  EV_WIFI_OK = 2,
  EV_WIFI_LOST = 3,
  EV_TEST_FAIL = 4,
  EV_ROUTER_RESET = 5,
  EV_AP_MODE = 6,

  EV_CONFIG_SAVED = 7,
  EV_SLEEP = 8,
  EV_FATAL = 9,

  EV_WDT_RESET = 10,
  EV_STUCK_BUTTON = 11,
  EV_GW_UNREACHABLE = 12,
  EV_LOW_HEAP = 13,
  EV_HEAP_RESTART = 14,
  EV_HTTP_ERROR = 15,

  EV_NET_DIAG = 16,

  EV_RESET_SKIPPED = 17
};

struct EventEntry {
  uint32_t uptimeSec;
  uint32_t epoch;
  uint16_t param;
  uint8_t code;
  uint8_t reserved;
};

constexpr uint8_t EVLOG_SIZE = 32;

constexpr uint32_t EVLOG_MAGIC = 0x42415A4DUL;
extern RTC_NOINIT_ATTR uint32_t rtcEvMagic;
extern RTC_NOINIT_ATTR uint32_t rtcEvNext;
extern RTC_NOINIT_ATTR EventEntry rtcEvents[EVLOG_SIZE];

constexpr uint16_t EVFILE_SIZE = 128;
constexpr char evLogPath[] = "/evlog.bin";
constexpr uint32_t EVFILE_MAGIC = 0x42415A46UL;

constexpr uint16_t EVFILE_VERSION = 1;

struct EvFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t evNextAtSave;
  uint32_t savedEpoch;
  uint32_t savedUptime;
};

extern RTC_NOINIT_ATTR uint32_t rtcSavedEvNext;

constexpr char NTP_SERVER[] = "hu.pool.ntp.org";

constexpr char NTP_TZ[] = "CET-1CEST,M3.5.0,M10.5.0/3";

constexpr uint32_t NTP_MIN_VALID_EPOCH = 1735689600UL;

void ensureNtp();

void ntpResetForColdBoot();

bool formatEpoch(uint32_t epoch, char* out, size_t outSize);

void logEvent(EventCode code, uint16_t param);

const char* eventName(uint8_t code);
const char* resetReasonName(esp_reset_reason_t r);

bool lastEventWas(uint8_t code);

bool stuckCycleAlreadyLogged(uint16_t which);

bool saveEventLog(const char* reason);

bool loadEventLogHeader(EvFileHeader& fej);

bool loadEventLogEntries(const EvFileHeader& fej, EventEntry* ki);

bool loadEventLogRange(const EvFileHeader& fej, uint32_t tol, uint16_t db,
                       EventEntry* ki);

void printEventLogs();
