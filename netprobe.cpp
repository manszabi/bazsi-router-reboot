#include "netprobe.h"

#include <HTTPClient.h>
#include <ESPping.h>
#include <WiFiUdp.h>
#include <string.h>
#include "esp_random.h"

#include "app_hooks.h"
#include "strutil.h"

static constexpr uint32_t PROBE_POLL_MS = 10;

static int s_lastHttpCode = 0;

int lastHttpCode() {
  return s_lastHttpCode;
}

uint16_t httpCodeLogParam(int code) {
  if (code >= 0) {
    return (code > 65535) ? (uint16_t)65535 : (uint16_t)code;
  }
  const int abszolut = -code;
  return (abszolut > 64535) ? (uint16_t)65535 : (uint16_t)(1000 + abszolut);
}

static_assert(PROBE_POLL_MS == 10, "a gomb-pollozas uteme a fomodullal egyezzen");

// Egy bájt legfeljebb timeoutMs várakozással; -1 = lejárt vagy megszakadt (FIN-t a core nem jelez).
static int readByteBounded(WiFiClient& stream, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    if (stream.available() > 0) {
      return stream.read();
    }

    if (!stream.connected()) {
      return -1;
    }
    resetbutton();
    wifiresetbutton();
    feedWatchdog();

    delay(PROBE_POLL_MS);
  }
  return -1;
}

static size_t readBounded(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  while (n < maxLen) {
    const int c = readByteBounded(stream, timeoutMs);
    if (c < 0) {
      break;
    }
    buf[n++] = (char)c;
  }
  return n;
}

static int hexDigitAnyCase(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Mint a readBounded(), de lebontja a chunked keretezést (a nyers streamben ott maradnak).
static size_t readChunked(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  // Véges: minden nem záró darab legalább egy bájttal növeli n-t, és n <= maxLen.
  for (;;) {
    uint32_t size = 0;
    bool sawDigit = false;
    bool inExt = false;
    for (;;) {
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      if (c == '\n') break;
      if (c == '\r' || inExt) continue;
      if (c == ';') { inExt = true; continue; }
      const int d = hexDigitAnyCase(c);

      if (d < 0) return n;
      if (size > (0xFFFFFFFFu - (uint32_t)d) / 16u) return n;
      size = size * 16u + (uint32_t)d;
      sawDigit = true;
    }
    if (!sawDigit || size == 0) {
      return n;
    }
    for (uint32_t i = 0; i < size; i++) {
      if (n >= maxLen) {
        return n;
      }
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      buf[n++] = (char)c;
    }

    for (uint8_t i = 0; i < 2; i++) {
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      if (c == '\n') break;
    }
  }
}

bool testInternetHTTP(const char* url, const char* expectedResponse) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout((int32_t)HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout((uint16_t)HTTP_RESPONSE_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    Serial.println("Error: HTTP begin failed");
    s_lastHttpCode = HTTP_CODE_BEGIN_FAILED;
    return false;
  }

  static const char* kCollectHeaders[] = { "Transfer-Encoding" };
  http.collectHeaders(kCollectHeaders, 1);

  const int httpCode = http.GET();
  s_lastHttpCode = httpCode;
  bool result = false;

  if (expectedResponse[0] == '\0') {
    result = (httpCode == HTTP_CODE_NO_CONTENT);

    if (!result) {
      Serial.print("Error on HTTP request, code: ");
      Serial.println(httpCode);
    }
  } else if (httpCode == HTTP_CODE_OK) {
    const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    const int len = http.getSize();
    if (len > (int)HTTP_MAX_PAYLOAD) {
      Serial.print("Unexpected payload size: ");
      Serial.println(len);
    } else {
      const size_t want = (len >= 0) ? (size_t)len : HTTP_MAX_PAYLOAD;
      char payload[HTTP_MAX_PAYLOAD + 1];

      const size_t n = chunked
                         ? readChunked(client, payload, HTTP_MAX_PAYLOAD, HTTP_READ_TIMEOUT_MS)
                         : readBounded(client, payload, want, HTTP_READ_TIMEOUT_MS);
      payload[n] = '\0';
      trimInPlace(payload);
      result = (strcmp(payload, expectedResponse) == 0);

      if (!result) {
        Serial.println(payload);
        Serial.println("Hamis ertek!");
      }
    }
  } else {
    Serial.print("Error on HTTP request, code: ");
    Serial.println(httpCode);
  }

  http.end();
  return result;
}

// Az ESPping 1.0.5 el sem küldött csomagra is igazat adhat: ezt az averageTime() > 0 szűri.
bool testInternetPing(const IPAddress& target, const char* targetName) {
  Serial.print("Ping teszt futtatasa (");
  Serial.print(targetName);
  Serial.print(" - ");
  Serial.print(target);
  Serial.println(")...");
  uint8_t successCount = 0;

  for (uint8_t j = 0; j < PING_ATTEMPTS; j++) {
    const bool pingOK = Ping.ping(target, 1) && Ping.averageTime() > 0.0f;
    Serial.print("Ping ");
    Serial.print(j + 1);
    if (pingOK) {
      Serial.println(" sikeres.");
      successCount++;
    } else {
      Serial.println(" sikertelen.");
      if (j == 0) {
        Serial.println("⚠️ Elso ping hiba - lehet, hogy a halozat ebred.");
      }
    }
    // Korai kilépés, ha az eredmény már eldőlt.
    const uint8_t remaining = PING_ATTEMPTS - (j + 1);
    if (successCount >= PING_MIN_SUCCESS || successCount + remaining < PING_MIN_SUCCESS) {
      break;
    }
    waitWithButtons(PING_GAP_MS);
  }
  const bool ok = successCount >= PING_MIN_SUCCESS;
  Serial.println(ok ? "✅ Ping teszt sikeres." : "❌ Ping teszt sikertelen - halozati problema valoszinu.");
  return ok;
}

// Fejléc (12 bájt): azonosító, RD jelző, egy kérdés; utána a név címkékre bontva, A típus, IN osztály.
size_t buildDnsQuery(uint16_t id, const char* name, uint8_t* out, size_t outSize) {
  const size_t nameLen = strlen(name);
  const size_t total = 12 + nameLen + 2 + 4;
  if (nameLen == 0 || total > outSize) {
    return 0;
  }
  memset(out, 0, 12);
  out[0] = (uint8_t)(id >> 8);
  out[1] = (uint8_t)(id & 0xFF);
  out[2] = 0x01;
  out[5] = 1;
  size_t i = 12;
  const char* cimke = name;
  for (;;) {
    const char* pont = strchr(cimke, '.');
    const size_t hossz = pont ? (size_t)(pont - cimke) : strlen(cimke);
    if (hossz == 0 || hossz > 63) {
      return 0;
    }
    out[i++] = (uint8_t)hossz;
    memcpy(out + i, cimke, hossz);
    i += hossz;
    if (!pont) {
      break;
    }
    cimke = pont + 1;
  }
  out[i++] = 0;
  out[i++] = 0;
  out[i++] = 1;
  out[i++] = 0;
  out[i++] = 1;
  return i;
}

bool dnsResponseOk(const uint8_t* buf, size_t len, uint16_t id) {
  if (len < 12) {
    return false;
  }
  const uint16_t kapottId = (uint16_t)((buf[0] << 8) | buf[1]);
  const bool valasz = (buf[2] & 0x80) != 0;
  const bool szabvanyos = (buf[2] & 0x78) == 0;
  const uint16_t kerdesek = (uint16_t)((buf[4] << 8) | buf[5]);
  const uint8_t hibakod = buf[3] & 0x0F;
  return kapottId == id && valasz && szabvanyos && kerdesek == 1 && (hibakod == 0 || hibakod == 3);
}

bool testDnsServer(const IPAddress& server, const char* name, const char* serverName) {
  Serial.print("DNS teszt (");
  Serial.print(serverName);
  Serial.print(" - ");
  Serial.print(server);
  Serial.println(")...");
  WiFiUDP udp;
  if (!udp.begin(0)) {
    Serial.println("DNS teszt: a UDP socket nem nyithato.");
    return false;
  }
  uint8_t csomag[DNS_MAX_PACKET];
  bool ok = false;
  for (uint8_t proba = 0; proba < DNS_ATTEMPTS && !ok; proba++) {
    const uint16_t id = (uint16_t)(esp_random() & 0xFFFF);
    const size_t n = buildDnsQuery(id, name, csomag, sizeof(csomag));
    if (n == 0 || !udp.beginPacket(server, DNS_PORT) || udp.write(csomag, n) != n
        || !udp.endPacket()) {
      continue;
    }
    const uint32_t start = millis();
    while (!ok && millis() - start < DNS_TIMEOUT_MS) {
      if (udp.parsePacket() > 0) {
        const bool forras = udp.remoteIP() == server && udp.remotePort() == DNS_PORT;
        const int kapott = udp.read(csomag, sizeof(csomag));
        udp.clear();
        ok = forras && kapott > 0 && dnsResponseOk(csomag, (size_t)kapott, id);
      }
      resetbutton();
      wifiresetbutton();
      feedWatchdog();
      delay(PROBE_POLL_MS);
    }
  }
  udp.stop();
  Serial.println(ok ? "✅ DNS teszt sikeres." : "❌ DNS teszt sikertelen.");
  return ok;
}
