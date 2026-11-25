// libFuzzer célpontok (FUZZ_TARGET 1-5): POST, titok-dekódolás, naplófájl, konfig olvasás, DNS-válasz.
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

#include "../limits_config.h"
#include "../configstore.h"
#include "../secret.h"
#include "../eventlog.h"
#include "../webportal.h"
#include "../sync.h"
#include "../netprobe.h"

extern char ssid[]; extern char pass[]; extern char ipStr[]; extern char gatewayStr[];
enum DeviceMode : uint8_t;
extern DeviceMode deviceMode;
extern std::map<std::string, ArRequestHandlerFunction> g_handlers;

static void fuzzReset() {
  g_fs.clear();
  g_fsMountOk = true; g_fsWritable = true; g_fsRemoveOk = true;
  g_fsReadable = true; g_fsCapacity = 0; g_fsShortReadSkip = 0;
  g_millis = 1;
  g_log.clear(); g_serialLog.clear();
  ssid[0] = pass[0] = ipStr[0] = gatewayStr[0] = '\0';
  rtcEvMagic = 0; rtcEvNext = 0;
  clearRestartRequest(); endConfigWrite();
  setFilesystemReady(true);
}

#if FUZZ_TARGET == 1

static const char* kNames[] = { "ssid", "pass", "ip", "gateway" };

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static bool inited = false;
  if (!inited) { startWebPortal(); inited = true; }
  fuzzReset();

  AsyncWebServerRequest req;
  size_t i = 0;
  std::string szabadNev;
  while (i + 2 <= size && req.params() < 24) {
    const uint8_t nevIdx = data[i++];
    size_t len = data[i++];
    if (len > size - i) len = size - i;
    std::string ertek((const char*)data + i, len);
    i += len;

    const bool post = (nevIdx & 0x80) == 0;
    if ((nevIdx & 0x7F) < 4) {
      req.addParam(kNames[nevIdx & 0x7F], ertek.c_str(), post);
    } else {
      szabadNev = "f" + std::to_string(nevIdx);
      req.addParam(szabadNev.c_str(), ertek.c_str(), post);
    }
  }
  try {
    g_handlers["/#2"](&req);
  } catch (RestartSignal&) {
  } catch (DeepSleepSignal&) {
  }
  return 0;
}

#elif FUZZ_TARGET == 2

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzReset();

  char buf[PASS_MAX_LEN + 1];
  size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
  memcpy(buf, data, n);
  buf[n] = '\0';
  decodeSecretInPlace(buf);

  char out[PASS_MAX_LEN * 2 + 8];
  encodeSecret(buf, out, sizeof(out));
  return 0;
}

#elif FUZZ_TARGET == 3

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzReset();
  g_fs["/evlog.bin"] = std::string((const char*)data, size);
  EvFileHeader fej;
  memset(&fej, 0, sizeof(fej));
  if (loadEventLogHeader(fej)) {
    EventEntry puffer[EVLOG_SIZE];
    memset(puffer, 0, sizeof(puffer));
    const uint16_t db = fej.count < EVLOG_SIZE ? fej.count : EVLOG_SIZE;
    loadEventLogRange(fej, 0, db, puffer);
  }

  EvFileHeader hamis;
  memset(&hamis, 0, sizeof(hamis));
  if (size >= 2) { hamis.count = (uint8_t)data[0]; hamis.version = data[1]; }
  EventEntry puffer2[EVLOG_SIZE];
  memset(puffer2, 0, sizeof(puffer2));
  loadEventLogRange(hamis, 0, hamis.count < EVLOG_SIZE ? hamis.count : EVLOG_SIZE, puffer2);
  return 0;
}

#elif FUZZ_TARGET == 4

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzReset();
  const std::string tartalom((const char*)data, size);
  g_fs["/ssid.txt"] = tartalom;
  g_fs["/pass.txt"] = tartalom;
  g_fs["/ip.txt"] = tartalom;
  g_fs["/gateway.txt"] = tartalom;

  char s[SSID_MAX_LEN + 1];
  char p[PASS_MAX_LEN + 1];
  char ip[IPSTR_MAX_LEN + 1];
  char gw[IPSTR_MAX_LEN + 1];
  readConfigValue(LittleFS, "/ssid.txt", s, sizeof(s));
  readConfigValue(LittleFS, "/pass.txt", p, sizeof(p));
  readConfigValue(LittleFS, "/ip.txt", ip, sizeof(ip));
  readConfigValue(LittleFS, "/gateway.txt", gw, sizeof(gw));

  fileMatches(LittleFS, "/ssid.txt", s, strlen(s));
  return 0;
}
#elif FUZZ_TARGET == 5

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const uint16_t id = size >= 2 ? (uint16_t)((data[0] << 8) | data[1]) : 0;
  (void)dnsResponseOk(data, size, id);
  char nev[80];
  const size_t n = size < sizeof(nev) - 1 ? size : sizeof(nev) - 1;
  memcpy(nev, data, n);
  nev[n] = '\0';
  uint8_t kerdes[DNS_MAX_PACKET];
  (void)buildDnsQuery(id, nev, kerdes, (size >= 1 ? data[0] : 0) % sizeof(kerdes));
  return 0;
}

#else
#error "FUZZ_TARGET 1..5 kozul valassz"
#endif
