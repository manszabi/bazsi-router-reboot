#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <string>
#define HTTP_CODE_OK 200
#define HTTP_CODE_NO_CONTENT 204
// Scriptelhető HTTP válasz a tesztekhez
extern int         g_httpCode;
extern std::string g_httpBody;
extern int         g_httpSize;   // -1 = nincs Content-Length, egyébként az értéke
extern bool        g_httpBeginOk;
// A valodi http.GET() a connect (5 mp) es a valasz (10 mp) timeoutjaig BLOKKOL,
// es kozben senki nem eteti a watchdogot. A sketch epp erre meretezte a 90 mp-es
// timeoutot, tehat a harnessnek modelleznie kell.
extern uint32_t    g_httpOkMs;      // sikeres keres ideje
extern uint32_t    g_httpFailMs;    // sikertelen: connect + valasz timeout
// A lekert URL-ek sorrendje: enelkul nem lehetne regresszioval vedeni,
// hogy az eszkalacio tenyleg tobb kulonbozo vegpontot probal vegig.
extern std::vector<std::string> g_httpUrls;
// Chunked valasz: a szerver nem Content-Length-t kuld, hanem darabhataroljakat,
// es azok a NYERS streamben is ott vannak - a HTTPClient csak a getString() /
// writeToStream() utjan bontja le oket. A sketch egyiket sem hasznalja, tehat
// neki maganak kell. g_httpChunkSize: hany bajtos darabokra vagjuk a torzset
// (0 = egyetlen darab).
extern bool     g_httpChunked;
extern size_t   g_httpChunkSize;
// Ha nem ures, ez megy ki a nyers streamre a g_httpBody helyett. Igy modellezheto
// a szabalytalan keretezes is (pl. hexa helyett szemet a meret sorban).
extern std::string g_httpRawOverride;

class HTTPClient {
public:
  // core 3.3.11: bool begin(NetworkClient &client, String url); WiFiClient == NetworkClient
  bool begin(WiFiClient& c, const char* url) {
    g_httpUrls.push_back(url ? url : "");
    if (!g_httpBeginOk) return false;
    c.setData(rawStream());
    return true;
  }
  int GET() {
    // Minden 2xx valasz ugyanolyan gyorsan erkezik; a lassu eset a timeout.
    const bool ok = (g_httpCode >= 200 && g_httpCode < 300);
    g_millis += ok ? g_httpOkMs : g_httpFailMs;
    return g_httpCode;
  }
  // Chunked eseten a valodi HTTPClient _size-a -1 marad (HTTPClient.cpp: a
  // Content-Length hianyaban nem allitja be).
  int getSize() {
    if (g_httpChunked) return -1;
    return g_httpSize >= -1 ? g_httpSize : (int)g_httpBody.size();
  }
  // core: a collectHeaders() a VALASZ fejleceibol gyujt, a header() ezeket adja
  // vissza (a be nem gyujtott nevekre ures sztringet).
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
  void setTimeout(uint16_t) {}       // core: uint16_t
  void setConnectTimeout(int32_t) {} // core: int32_t
  void setReuse(bool) {}

private:
  std::vector<std::string> keys_;

  // A nyers stream tartalma: chunked eseten a keretbajtokkal egyutt.
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
