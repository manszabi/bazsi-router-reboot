// A mentett konfiguráció (SSID, jelszó, IP, gateway) és a LittleFS olvasás/írás.
#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include "limits_config.h"

extern char ssid[SSID_MAX_LEN + 1];
extern char pass[PASS_MAX_LEN + 1];
extern char ipStr[IPSTR_MAX_LEN + 1];
extern char gatewayStr[IPSTR_MAX_LEN + 1];

extern const char ssidPath[];
extern const char passPath[];
extern const char ipPath[];
extern const char gatewayPath[];

// Opcionális kapcsoló ("1"/"0"): Wi-Fi-kiesés alatt saját hálózat az állapotoldallal; hiányzó fájl = ki.
extern const char outageApPath[];
extern bool outageApEnabled;

// A hiányzó fájl nem hiba (első indulás, wifireset után).
enum ConfigStatus : uint8_t {
  CONFIG_OK = 0,
  CONFIG_MISSING = 1,
  CONFIG_ERROR = 2
};

bool initLittleFS();

bool filesystemReady();

// Csak a host tesztek hívják.
void setFilesystemReady(bool ready);

bool isUsableIPv4(const IPAddress& addr);

// Statikus IP és gateway párosa: azonos /24, nem .0/.255, nem azonos.
bool staticPairValid(const IPAddress& ip, const IPAddress& gateway);

ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize);

// Mint a readConfigValue(), de csak a CR-t vágja le (a szóköz a jelszó része lehet).
ConfigStatus readSecretValue(fs::FS& fs, const char* path, char* out, size_t outSize);

// Írás visszaolvasásos ellenőrzéssel.
bool writeConfigValue(fs::FS& fs, const char* path, const char* message);

bool clearConfigValue(fs::FS& fs, const char* path);

bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len);
