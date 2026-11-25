#include "netprobe.h"

#include <HTTPClient.h>
#include <ESPping.h>
#include <string.h>

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
        Serial.println("Hamis érték!");
      }
    }
  } else {
    Serial.print("Error on HTTP request, code: ");
    Serial.println(httpCode);
  }

  http.end();
  return result;
}

bool testInternetPing(const IPAddress& target, const char* targetName) {
  Serial.print("Ping teszt futtatása (");
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
        Serial.println("⚠️ Első ping hiba — lehet, hogy a hálózat ébred.");
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
  Serial.println(ok ? "✅ Ping teszt sikeres." : "❌ Ping teszt sikertelen — hálózati probléma valószínű.");
  return ok;
}
