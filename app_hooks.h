// Amit a főmodul nyújt a többi modulnak (watchdog, gombok, várakozás, diagnosztika).
#pragma once

#include <stdint.h>

void feedWatchdog();

void resetbutton();
void wifiresetbutton();

void waitWithButtons(uint32_t duration);

void printUptime();

// A /log oldal számlálói egy pillanatképben.
struct DiagCounters {
  uint32_t wdtResets;
  uint32_t wdtLimit;
  uint32_t retryRounds;
  uint32_t retryLimit;
};
DiagCounters diagCounters();
