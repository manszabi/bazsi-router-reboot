#pragma once

#include <Arduino.h>
#include <stdint.h>

extern portMUX_TYPE evLogMux;

bool beginConfigWrite();

void endConfigWrite();

bool configWriteInProgress();

void requestRestart(uint32_t delayMs);

bool restartRequestDue(uint32_t now);

bool restartRequested();

void clearRestartRequest();

extern volatile uint32_t apDeadline;
