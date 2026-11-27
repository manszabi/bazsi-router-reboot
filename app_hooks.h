// Amit a főmodul nyújt a többi modulnak (watchdog, gombok, várakozás, diagnosztika).
#pragma once

#include <stdint.h>

void feedWatchdog();

void resetbutton();
void wifiresetbutton();

void waitWithButtons(uint32_t duration);

void printUptime();

// A /log és az állapotoldal számlálói egy pillanatképben (a másik taskból olvasva).
struct DiagCounters {
  uint32_t wdtResets;
  uint32_t wdtLimit;
  uint32_t wifiStep;
  uint32_t wifiOutageMin;
  bool wifiUp;
  uint32_t heapRestarts;
  bool netOutage;
  uint32_t netOutageMin;
  uint32_t netResets;
  bool dnsSuspect;
  uint32_t dnsSuspectMin;
  uint32_t dnsResets;
  bool resetRunning;
};
DiagCounters diagCounters();
