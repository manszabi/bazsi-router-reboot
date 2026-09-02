#include "secret.h"

#include <string.h>

// --- A mentett jelszó összekeverése ----------------------------------------
//
// Cél, pontosan körülhatárolva: egy flash dumpon futtatott `strings` NE adjon
// használható jelszót, és egy kimásolt /pass.txt más lapkán se működjön.
//
// Amit NEM ad: ez nem titkosítás. Aki kódot tud futtatni az eszközön (a C3-ban
// beépített USB Serial/JTAG-gel vagy saját sketch-csel), az a visszafejtett
// jelszót kiolvassa a RAM-ból - a művelet ugyanis magán az eszközön történik.
// Az egyetlen valódi védelem a flash titkosítás (eFuse-ban tárolt kulccsal).
//
// Formátum: "v1:" + kisbetűs hexa. Az előtag nélküli fájl régi, sima szöveges
// mentés; azt továbbra is elfogadjuk, különben egy frissítés használhatatlanná
// tenné a már beállított eszközöket.
//
// A kulcsfolyam magjában ott van az eFuse MAC is (esp_efuse_mac_get_default(),
// Esp.cpp). Az eFuse NEM a flashben van, tehát egy önmagában kimásolt
// flash-tartalom kevés hozzá, és a nyilvános forráskódból írt általános
// dekóder sem elég: az adott chip is kell.

static uint32_t secretSeed() {
  const uint64_t mac = ESP.getEfuseMac();  // 6 bájt, a felső 2 nulla
  const uint32_t seed = SECRET_SALT ^ (uint32_t)mac ^ (uint32_t)(mac >> 32);
  // A xorshift a 0 állapotból soha nem lép ki - ezt ki kell zárni.
  return seed != 0 ? seed : SECRET_SALT;
}

// Determinisztikus kulcsfolyam. Pozíciófüggő, tehát az ismétlődő karakterek
// sem adnak ismétlődő bájtokat a fájlban.
static uint32_t xorshift32(uint32_t& x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

bool encodeSecret(const char* plain, char* out, size_t outSize) {
  const size_t len = strlen(plain);
  if (outSize < SECRET_PREFIX_LEN + 2 * len + 1) {
    return false;
  }
  memcpy(out, SECRET_PREFIX, SECRET_PREFIX_LEN);
  uint32_t state = secretSeed();
  // NEM "HEX": a Print.h-ban az egy makro (#define HEX 16), tehat abbol
  // "static const char 16[]" lenne. A core makrói (HEX, DEC, OCT, BIN) minden
  // sketchre ravonatkoznak - a lokalis nevek nem utkozhetnek veluk.
  static const char kHexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    const uint8_t b = (uint8_t)plain[i] ^ (uint8_t)(xorshift32(state) & 0xFF);
    out[SECRET_PREFIX_LEN + 2 * i]     = kHexDigits[b >> 4];
    out[SECRET_PREFIX_LEN + 2 * i + 1] = kHexDigits[b & 0x0F];
  }
  out[SECRET_PREFIX_LEN + 2 * len] = '\0';
  return true;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // szándékosan csak kisbetűs: ezt írjuk ki
}

// Helyben dekódol. Ha a tartalom nem a mi formátumunk, VÁLTOZATLANUL hagyja -
// így a régi, sima szöveges mentések is működnek, és az sem baj, ha valakinek
// történetesen "v1:" a jelszava.
//
// Szándékosan NEM végzetes hiba a hibás tartalom: rossz jelszóval a Wi-Fi
// egyszerűen nem jön össze, és az eszköz a szokásos úton AP módba kerül, ahol
// újra beállítható. Ez öngyógyul, a villogó LED nem.
void decodeSecretInPlace(char* buf) {
  if (strncmp(buf, SECRET_PREFIX, SECRET_PREFIX_LEN) != 0) {
    return;  // régi, sima szöveges mentés
  }
  const char* hex = buf + SECRET_PREFIX_LEN;
  const size_t hexLen = strlen(hex);
  if (hexLen % 2 != 0) {
    return;
  }
  for (size_t i = 0; i < hexLen; i++) {
    if (hexVal(hex[i]) < 0) {
      return;
    }
  }
  // A kiírási index (i) mindig kisebb az olvasásinál (3 + 2i), ezért a helyben
  // dekódolás előrefelé haladva biztonságos.
  uint32_t state = secretSeed();
  const size_t n = hexLen / 2;
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = (uint8_t)((hexVal(hex[2 * i]) << 4) | hexVal(hex[2 * i + 1]));
    buf[i] = (char)(b ^ (uint8_t)(xorshift32(state) & 0xFF));
  }
  buf[n] = '\0';
}
