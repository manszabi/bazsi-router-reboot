#pragma once
#include <Arduino.h>
#include <WiFi.h>
#define HTTP_CODE_OK 200
// Scriptelhető HTTP válasz a tesztekhez
extern int         g_httpCode;
extern std::string g_httpBody;
extern int         g_httpSize;   // -1 = ismeretlen (chunked), egyébként Content-Length
extern bool        g_httpBeginOk;
// A valodi http.GET() a connect (5 mp) es a valasz (10 mp) timeoutjaig BLOKKOL,
// es kozben senki nem eteti a watchdogot. A sketch epp erre meretezte a 90 mp-es
// timeoutot, tehat a harnessnek modelleznie kell.
extern uint32_t    g_httpOkMs;      // sikeres keres ideje
extern uint32_t    g_httpFailMs;    // sikertelen: connect + valasz timeout

class HTTPClient {
public:
  // core 3.3.11: bool begin(NetworkClient &client, String url); WiFiClient == NetworkClient
  bool begin(WiFiClient& c, const char* url) {
    (void)url;
    if (!g_httpBeginOk) return false;
    c.setData(g_httpBody);
    return true;
  }
  int GET() {
    g_millis += (g_httpCode == HTTP_CODE_OK) ? g_httpOkMs : g_httpFailMs;
    return g_httpCode;
  }
  int getSize() { return g_httpSize >= -1 ? g_httpSize : (int)g_httpBody.size(); }
  void end() {}
  void setTimeout(uint16_t) {}       // core: uint16_t
  void setConnectTimeout(int32_t) {} // core: int32_t
  void setReuse(bool) {}
};
