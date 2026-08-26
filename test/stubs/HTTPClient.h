#pragma once
#include <Arduino.h>
#include <WiFi.h>
#define HTTP_CODE_OK 200
// Scriptelhető HTTP válasz a tesztekhez
extern int         g_httpCode;
extern std::string g_httpBody;
extern int         g_httpSize;   // -1 = ismeretlen (chunked), egyébként Content-Length
extern bool        g_httpBeginOk;

class HTTPClient {
public:
  // core 3.3.11: bool begin(NetworkClient &client, String url); WiFiClient == NetworkClient
  bool begin(WiFiClient& c, const char* url) {
    (void)url;
    if (!g_httpBeginOk) return false;
    c.setData(g_httpBody);
    return true;
  }
  int GET() { return g_httpCode; }
  int getSize() { return g_httpSize >= -1 ? g_httpSize : (int)g_httpBody.size(); }
  void end() {}
  void setTimeout(uint16_t) {}       // core: uint16_t
  void setConnectTimeout(int32_t) {} // core: int32_t
  void setReuse(bool) {}
};
