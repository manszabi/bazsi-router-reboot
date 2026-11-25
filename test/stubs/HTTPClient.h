// HTTPClient modell: szkriptelhető válasz, chunked keretezés, blokkolási idő.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <string>
#define HTTP_CODE_OK 200
#define HTTP_CODE_NO_CONTENT 204

extern int         g_httpCode;
extern std::string g_httpBody;
extern int         g_httpSize;
extern bool        g_httpBeginOk;

extern uint32_t    g_httpOkMs;
extern uint32_t    g_httpFailMs;

extern std::vector<std::string> g_httpUrls;

extern bool     g_httpChunked;
extern size_t   g_httpChunkSize;

extern std::string g_httpRawOverride;

// Igaz: minden végpont a saját, helyes válaszát adja (élő internet).
extern bool g_httpAutoOk;

class HTTPClient {
public:

  bool begin(WiFiClient& c, const char* url) {
    g_httpUrls.push_back(url ? url : "");
    if (!g_httpBeginOk) return false;
    if (g_httpAutoOk) autoValasz(url ? url : "");
    c.setData(rawStream());
    return true;
  }
  int GET() {
    const bool ok = (g_httpCode >= 200 && g_httpCode < 300);
    const uint32_t eltelt = ok ? g_httpOkMs : g_httpFailMs;
    g_millis += eltelt;

    wdtAdvanceTime(eltelt);
    return g_httpCode;
  }

  int getSize() {
    if (g_httpChunked) return -1;
    return g_httpSize >= -1 ? g_httpSize : (int)g_httpBody.size();
  }

  void collectHeaders(const char* keys[], size_t count) {
    keys_.clear();
    for (size_t i = 0; i < count; i++) keys_.push_back(keys[i] ? keys[i] : "");
  }
  String header(const char* name) {
    for (const auto& k : keys_) {
      if (strcasecmp(k.c_str(), name) == 0 && strcasecmp(k.c_str(), "Transfer-Encoding") == 0) {
        return String(g_httpChunked ? "chunked" : "");
      }
    }
    return String("");
  }
  void end() {}
  void setTimeout(uint16_t) {}
  void setConnectTimeout(int32_t) {}
  void setReuse(bool) {}

private:
  std::vector<std::string> keys_;

  static void autoValasz(const std::string& url) {
    g_httpCode = 200; g_httpSize = -2; g_httpChunked = false; g_httpRawOverride.clear();
    if (url.find("generate_204") != std::string::npos) { g_httpCode = 204; g_httpBody = ""; }
    else if (url.find("msftconnecttest") != std::string::npos) g_httpBody = "Microsoft Connect Test";
    else if (url.find("firefox") != std::string::npos) g_httpBody = "success";
    else if (url.find("gnome") != std::string::npos) g_httpBody = "NetworkManager is online";
  }

  static std::string rawStream() {
    if (!g_httpRawOverride.empty()) return g_httpRawOverride;
    if (!g_httpChunked) return g_httpBody;
    std::string out;
    const size_t step = g_httpChunkSize ? g_httpChunkSize : g_httpBody.size();
    for (size_t i = 0; i < g_httpBody.size(); i += step) {
      const std::string part = g_httpBody.substr(i, step);
      char hdr[24];
      snprintf(hdr, sizeof(hdr), "%zx\r\n", part.size());
      out += hdr;
      out += part;
      out += "\r\n";
    }
    out += "0\r\n\r\n";
    return out;
  }
};
