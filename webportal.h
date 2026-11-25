#pragma once

#include <ESPAsyncWebServer.h>
#include <stdint.h>

constexpr uint32_t AP_TIMEOUT_MS = 5 * 60 * 1000;

void startWebPortal();

void stopWebPortal();

void announceApPassword();

void touchApDeadline();
