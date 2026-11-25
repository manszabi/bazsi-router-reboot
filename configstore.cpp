#include "configstore.h"

#include <LittleFS.h>
#include <string.h>

#include "strutil.h"

static bool fsReady = false;

bool filesystemReady() { return fsReady; }
void setFilesystemReady(bool ready) { fsReady = ready; }

char ssid[SSID_MAX_LEN + 1]        = { 0 };
char pass[PASS_MAX_LEN + 1]        = { 0 };
char ipStr[IPSTR_MAX_LEN + 1]      = { 0 };
char gatewayStr[IPSTR_MAX_LEN + 1] = { 0 };
const char ssidPath[] = "/ssid.txt";
const char passPath[] = "/pass.txt";
const char ipPath[] = "/ip.txt";
const char gatewayPath[] = "/gateway.txt";

bool isUsableIPv4(const IPAddress& addr) {
  return (uint32_t)addr != 0;
}

// Az operator[] az oktetteket hálózati sorrendben adja.
bool staticPairValid(const IPAddress& ip, const IPAddress& gateway) {
  if (!isUsableIPv4(ip) || !isUsableIPv4(gateway)) {
    return false;
  }

  for (int i = 0; i < 3; i++) {
    if (ip[i] != gateway[i]) {
      return false;
    }
  }
  if (ip[3] == 0 || ip[3] == 255 || gateway[3] == 0 || gateway[3] == 255) {
    return false;
  }
  return ip[3] != gateway[3];
}

bool initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount FAILED (a formázási kísérlet után is).");
    Serial.println("Valószínű ok: a használt partíciós séma nem tartalmaz");
    Serial.println("'spiffs' cimkéju partíciót. Használd a partitions_custom.csv-t,");
    Serial.println("vagy az Arduino IDE-ben egy SPIFFS-et tartalmazó sémát");
    Serial.println("(Tools > Partition Scheme).");
    fsReady = false;
    return false;
  }
  Serial.print("LittleFS mounted, used ");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" / ");
  Serial.print(LittleFS.totalBytes());
  Serial.println(" bytes");
  fsReady = true;
  return true;
}

bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len) {
  File file = fs.open(path);
  if (!file) {
    return false;
  }
  if (file.size() != len) {
    file.close();
    return false;
  }
  char chunk[32];
  size_t off = 0;
  while (off < len) {
    const size_t want = (len - off > sizeof(chunk)) ? sizeof(chunk) : (len - off);
    const size_t got = file.read((uint8_t*)chunk, want);
    if (got != want || memcmp(chunk, value + off, got) != 0) {
      file.close();
      return false;
    }
    off += got;
  }
  file.close();
  return true;
}

static ConfigStatus readValue(fs::FS& fs, const char* path, char* out, size_t outSize,
                              bool trimAll) {
  out[0] = '\0';

  if (!fs.exists(path)) {
    Serial.printf("- %s missing (no config yet)\r\n", path);
    return CONFIG_MISSING;
  }

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.printf("- failed to open %s for reading\r\n", path);
    if (file) {
      file.close();
    }
    return CONFIG_ERROR;
  }

  const size_t fileSize = file.size();
  const size_t toRead = (fileSize < outSize - 1) ? fileSize : outSize - 1;
  const size_t n = file.read((uint8_t*)out, toRead);
  file.close();
  if (n != toRead) {
    Serial.printf("- short read on %s (%u / %u)\r\n", path, (unsigned)n, (unsigned)toRead);
    out[0] = '\0';
    return CONFIG_ERROR;
  }
  out[n] = '\0';

  char* nl = strchr(out, '\n');
  if (nl != nullptr) {
    *nl = '\0';
  }
  if (trimAll) {
    trimInPlace(out);
  } else {
    const size_t hossz = strlen(out);
    if (hossz > 0 && out[hossz - 1] == '\r') {
      out[hossz - 1] = '\0';
    }
  }

  return CONFIG_OK;
}

ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize) {
  return readValue(fs, path, out, outSize, true);
}

ConfigStatus readSecretValue(fs::FS& fs, const char* path, char* out, size_t outSize) {
  return readValue(fs, path, out, outSize, false);
}

bool writeConfigValue(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);
  const size_t len = strlen(message);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return false;
  }

  const size_t written = (len > 0) ? file.print(message) : 0;
  file.flush();
  file.close();

  if (written != len) {
    Serial.print("- short write: ");
    Serial.print((unsigned)written);
    Serial.print(" / ");
    Serial.print((unsigned)len);
    Serial.println(" bájt (megtelt a fájlrendszer?)");
    return false;
  }
  if (!fileMatches(fs, path, message, len)) {
    Serial.println("- verify FAILED: a visszaolvasott tartalom nem egyezik");
    return false;
  }
  Serial.println("- file written");
  return true;
}

// Üresre írás; ha az nem megy, törlés.
bool clearConfigValue(fs::FS& fs, const char* path) {
  Serial.printf("Clearing file: %s\r\n", path);
  if (writeConfigValue(fs, path, "")) {
    Serial.println("- file cleared");
    return true;
  }
  if (fs.remove(path)) {
    Serial.println("- file removed instead");
    return true;
  }
  Serial.println("- FAILED to clear file!");
  return false;
}
