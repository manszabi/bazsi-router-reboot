// Az AP beállító portál: űrlap, POST mentés, /log napló, /ping keep-alive.
#pragma once

#include <ESPAsyncWebServer.h>
#include <stdint.h>

// Tétlenség után az AP mód elalszik; minden kérés kitolja.
constexpr uint32_t AP_TIMEOUT_MS = 5 * 60 * 1000;

void startWebPortal();

void stopWebPortal();

// Az eszközönkénti AP jelszó kiírása a soros portra (ha már van).
void announceApPassword();

void touchApDeadline();
