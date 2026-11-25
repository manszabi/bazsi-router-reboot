#include "sync.h"

portMUX_TYPE evLogMux = portMUX_INITIALIZER_UNLOCKED;

// Csak az evLogMux alatt érhetők el (a jelző és a határidő párban változik).
static volatile bool     savingConfig   = false;
static volatile bool     restartPending = false;
static volatile uint32_t restartAt      = 0;

volatile uint32_t apDeadline = 0;

bool beginConfigWrite() {
  bool acquired = false;
  portENTER_CRITICAL(&evLogMux);
  if (!savingConfig) {
    savingConfig = true;
    acquired = true;
  }
  portEXIT_CRITICAL(&evLogMux);
  return acquired;
}

void endConfigWrite() {
  portENTER_CRITICAL(&evLogMux);
  savingConfig = false;
  portEXIT_CRITICAL(&evLogMux);
}

void requestRestart(uint32_t delayMs) {
  const uint32_t at = millis() + delayMs;
  portENTER_CRITICAL(&evLogMux);
  restartAt = at;
  restartPending = true;
  portEXIT_CRITICAL(&evLogMux);
}

bool restartRequestDue(uint32_t now) {
  portENTER_CRITICAL(&evLogMux);
  const bool due = restartPending && (int32_t)(now - restartAt) >= 0;
  portEXIT_CRITICAL(&evLogMux);
  return due;
}

bool restartRequested() {
  portENTER_CRITICAL(&evLogMux);
  const bool p = restartPending;
  portEXIT_CRITICAL(&evLogMux);
  return p;
}

void clearRestartRequest() {
  portENTER_CRITICAL(&evLogMux);
  restartPending = false;
  portEXIT_CRITICAL(&evLogMux);
}

bool configWriteInProgress() {
  portENTER_CRITICAL(&evLogMux);
  const bool s = savingConfig;
  portEXIT_CRITICAL(&evLogMux);
  return s;
}
