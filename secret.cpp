#include "secret.h"

#include <string.h>

// Eszközfüggő mag: a MAC-ből; a kódolt fájl más lapkán nem dekódolható.
static uint32_t secretSeed() {
  const uint64_t mac = ESP.getEfuseMac();
  const uint32_t seed = SECRET_SALT ^ (uint32_t)mac ^ (uint32_t)(mac >> 32);

  return seed != 0 ? seed : SECRET_SALT;
}

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
  return -1;
}

void decodeSecretInPlace(char* buf) {
  if (strncmp(buf, SECRET_PREFIX, SECRET_PREFIX_LEN) != 0) {
    return;
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

  uint32_t state = secretSeed();
  const size_t n = hexLen / 2;
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = (uint8_t)((hexVal(hex[2 * i]) << 4) | hexVal(hex[2 * i + 1]));
    buf[i] = (char)(b ^ (uint8_t)(xorshift32(state) & 0xFF));
  }
  buf[n] = '\0';
}
