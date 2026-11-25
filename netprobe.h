// Internetkapcsolat mérése (HTTP végpont, ICMP ping). Csak mér: nem dönt, nem naplóz.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>

// Ping: 4 próbából legalább 2 sikere kell (egy elveszett csomag nem kiesés).
constexpr uint8_t  PING_ATTEMPTS    = 4;
constexpr uint8_t  PING_MIN_SUCCESS = 2;
constexpr uint32_t PING_GAP_MS      = 1000;

// HTTP: korlátos, időzáras olvasás (egy captive portal nagy válasza se fusson el).
constexpr size_t   HTTP_MAX_PAYLOAD         = 96;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS  = 5000;
constexpr uint32_t HTTP_RESPONSE_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_READ_TIMEOUT_MS     = 1500;

// Üres expectedResponse = 204 No Content ellenőrzés.
bool testInternetHTTP(const char* url, const char* expectedResponse);

// Az utolsó HTTP teszt kódja: negatív = kliens oldali hiba (HTTPClient).
constexpr int HTTP_CODE_BEGIN_FAILED = -100;
int lastHttpCode();

// Naplózható alak: nemnegatív változatlan, negatív -> 1000 + |kód|.
uint16_t httpCodeLogParam(int code);

bool testInternetPing(const IPAddress& target, const char* targetName);

// DNS: egyetlen A-kérdés közvetlenül egy szerverre (tartalék és gyorsítótár nélkül).
constexpr uint16_t DNS_PORT        = 53;
constexpr uint8_t  DNS_ATTEMPTS    = 2;
constexpr uint32_t DNS_TIMEOUT_MS  = 3000;
constexpr size_t   DNS_MAX_PACKET  = 512;

// A kérdés a bufferbe; 0 = nem fér el vagy hibás a név.
size_t buildDnsQuery(uint16_t id, const char* name, uint8_t* out, size_t outSize);

// Él-e a szerver: válasz a kérdésünkre, hibakód 0 (rendben) vagy 3 (nincs ilyen név).
bool dnsResponseOk(const uint8_t* buf, size_t len, uint16_t id);

bool testDnsServer(const IPAddress& server, const char* name, const char* serverName);
