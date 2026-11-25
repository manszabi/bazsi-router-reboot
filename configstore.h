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

enum ConfigStatus : uint8_t {
  CONFIG_OK = 0,
  CONFIG_MISSING = 1,
  CONFIG_ERROR = 2
};

bool initLittleFS();

bool filesystemReady();

void setFilesystemReady(bool ready);

bool isUsableIPv4(const IPAddress& addr);

bool staticPairValid(const IPAddress& ip, const IPAddress& gateway);

ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize);

ConfigStatus readSecretValue(fs::FS& fs, const char* path, char* out, size_t outSize);

bool writeConfigValue(fs::FS& fs, const char* path, const char* message);

bool clearConfigValue(fs::FS& fs, const char* path);

bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len);
