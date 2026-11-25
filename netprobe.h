#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>

constexpr uint8_t  PING_ATTEMPTS    = 4;
constexpr uint8_t  PING_MIN_SUCCESS = 2;
constexpr uint32_t PING_GAP_MS      = 1000;

constexpr size_t   HTTP_MAX_PAYLOAD         = 96;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS  = 5000;
constexpr uint32_t HTTP_RESPONSE_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_READ_TIMEOUT_MS     = 1500;

constexpr uint16_t HTTP_MAX_CHUNKS = HTTP_MAX_PAYLOAD + 2;

static_assert(HTTP_MAX_CHUNKS > HTTP_MAX_PAYLOAD,
              "a darabszam-korlat legyen tagabb a hasznos-teher korlatnal, "
              "kulonben a readChunked() zaro return-je elerhetove valik");

bool testInternetHTTP(const char* url, const char* expectedResponse);

constexpr int HTTP_CODE_BEGIN_FAILED = -100;
int lastHttpCode();

uint16_t httpCodeLogParam(int code);

bool testInternetPing(const IPAddress& target, const char* targetName);
