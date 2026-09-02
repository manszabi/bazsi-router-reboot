#include "configstore.h"

#include <LittleFS.h>
#include <string.h>

#include "strutil.h"

// Sikerult-e a LittleFS csatolasa. STATIC: kivulrol csak a filesystemReady()
// / setFilesystemReady() paron at erheto el.
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

// Használható-e ez a cím statikus IPv4 konfigurációnak?
//
// Az IPAddress::fromString() az IPv4 után IPv6-ot is megpróbál (IPAddress.cpp),
// ezért a "::1" vagy a "fe80::1" is érvényesnek látszik - és mindkettő befér a
// 15 karakteres mezőbe. Az eszköz viszont végig IPv4-en dolgozik (a gateway
// pingje, a HTTP tesztek, az 1.1.1.1-es tartalék DNS, a /24-es maszk), a
// WiFi.config() pedig az IPAddress uint32_t konverzióját használja
// (NetworkInterface.cpp:390), ami IPv6-ra 0-t ad
// (IPAddress.h:83). Vagyis egy IPv6 cím csendben DHCP-t vagy - ami rosszabb -
// egy 0.0.0.0-s gateway-t és DNS-t eredményezne. A 0.0.0.0 ugyanezt jelenti,
// ezért az sem fogadható el.
bool isUsableIPv4(const IPAddress& addr) {
  return (uint32_t)addr != 0;
}

// Whitespace levágása helyben, allokáció nélkül
// Initialize LittleFS
bool initLittleFS() {
  if (!LittleFS.begin(true)) {
    // A begin() csak ESP_FAIL esetén próbál formázni. Ha a partíció egyáltalán
    // nincs meg, ESP_ERR_NOT_FOUND jön, és formázás nélkül elbukik.
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

// A kiírt tartalom ellenőrzése visszaolvasással. Erre azért van szükség, mert
// a File::close() és a File::flush() is void: a lezáráskor jelentkező hibát
// (pl. megtelt fájlrendszer) másképp nem lehetne észrevenni.
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

// Egy konfigurációs érték beolvasása fix méretű bufferbe (String allokáció nélkül)
ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize) {
  out[0] = '\0';

  // A hiányzó fájl nem hiba: első indításkor és wifireset után is ez a helyzet.
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
  // Méret szerint olvasunk: a Stream::readBytesUntil() EOF-nál kivárná a teljes
  // 1 másodperces stream-timeoutot, fájlonként (indulásnál ez 4 mp veszteség).
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

  char* nl = strchr(out, '\n');  // csak az első sor érdekel
  if (nl != nullptr) {
    *nl = '\0';
  }
  trimInPlace(out);
  // Az üres tartalom is érvényes eredmény: a wifireset csonkolt fájlt hagy hátra.
  return CONFIG_OK;
}

// Írás ellenőrzéssel. true csak akkor, ha a tartalom vissza is olvasható.
bool writeConfigValue(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);
  const size_t len = strlen(message);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return false;
  }
  // Üres értéknél a print() jogosan ad 0-t; ez nem hiba, csak csonkolás.
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

// Érték törlése. Először csonkolással próbáljuk; ha az nem megy, a fájlt
// magát töröljük - a readConfigValue() a hiányzó fájlt is "nincs érték"-ként
// kezeli, tehát a végeredmény ugyanaz.
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
