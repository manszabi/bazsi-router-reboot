// A loop és az async_tcp task közös állapota, spinlock (evLogMux) alatt.
#pragma once

#include <Arduino.h>
#include <stdint.h>

extern portMUX_TYPE evLogMux;

// Konfigzár: igaz, ha megszereztük (atomikus). Mentés közben nincs alvás és újraindulás.
bool beginConfigWrite();

void endConfigWrite();

bool configWriteInProgress();

// Halasztott újraindítás (a portál mentése után).
void requestRestart(uint32_t delayMs);

bool restartRequestDue(uint32_t now);

bool restartRequested();

void clearRestartRequest();

// Az AP mód tétlenségi határideje (millis).
extern volatile uint32_t apDeadline;
