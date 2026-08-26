#pragma once
#include <Arduino.h>
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1
#define WIFI_AP 2

// Olvasható HTTP-törzset hordozó kliens (a HTTPClient stub tölti fel)
class WiFiClient : public Stream {
public:
  virtual bool connected() { return pos_ < data_.size(); }
  int read() override { return pos_ < data_.size() ? (unsigned char)data_[pos_++] : -1; }
  int available() override { return (int)(data_.size() - pos_); }
  void setData(const std::string& d) { data_ = d; pos_ = 0; }
private:
  std::string data_; size_t pos_ = 0;
};

// Wi-Fi rádió szimuláció.
struct WifiSim {
  bool willConnect = false;      // sikerül-e a csatlakozás
  // A hálózat csak ettől az időponttól elérhető (0 = azonnal). Ezzel
  // modellezhető, hogy a router az ESP után percekkel áll fel.
  uint32_t availableFrom = 0;
  uint32_t latencyMs = 500;      // mennyi idő alatt jön létre
  bool begun = false;            // fut-e a begin() óta kapcsolódás
  uint32_t beginAt = 0;
  int mode = 0;
  bool radioOff = true;
  // A netif statikus IP konfigja. disconnect(true) leállítja a WiFi-t,
  // ilyenkor a valódi ESP32 core-ban ez elveszik -> DHCP.
  bool staticApplied = false;
  IPAddress cfgIp, cfgGw, cfgDns1, cfgDns2;
  int beginCount = 0;
  int configCount = 0;
  int softApCount = 0;
  void reset() { *this = WifiSim(); }
};
extern WifiSim wifiSim;

class WiFiClass {
public:
  void mode(int m) {
    wifiSim.mode = m;
    simLog(m == WIFI_AP ? "WiFi.mode(AP)" : "WiFi.mode(STA)");
    if (m == WIFI_AP) { wifiSim.begun = false; }
  }
  void begin(const char* s, const char* p) {
    (void)p;
    wifiSim.begun = true; wifiSim.beginAt = g_millis; wifiSim.radioOff = false;
    wifiSim.beginCount++;
    simLog(std::string("WiFi.begin(") + (s ? s : "") + ")");
  }
  int status() {
    if (!wifiSim.begun || !wifiSim.willConnect) return WL_DISCONNECTED;
    if (g_millis < wifiSim.availableFrom) return WL_DISCONNECTED;
    // a társítás a begin() vagy a hálózat megjelenése közül a későbbitől indul
    const uint32_t from = wifiSim.beginAt > wifiSim.availableFrom
                          ? wifiSim.beginAt : wifiSim.availableFrom;
    return (g_millis - from) >= wifiSim.latencyMs ? WL_CONNECTED : WL_DISCONNECTED;
  }
  String SSID() { return String("TestNet"); }
  IPAddress localIP() { return wifiSim.staticApplied ? wifiSim.cfgIp : IPAddress(192,168,1,77); }
  int RSSI() { return -50; }
  bool config(IPAddress ip, IPAddress gw, IPAddress sn, IPAddress d1 = IPAddress(), IPAddress d2 = IPAddress()) {
    (void)sn;
    wifiSim.staticApplied = true; wifiSim.configCount++;
    wifiSim.cfgIp = ip; wifiSim.cfgGw = gw; wifiSim.cfgDns1 = d1; wifiSim.cfgDns2 = d2;
    simLog("WiFi.config(ip=" + ip.str() + ",gw=" + gw.str() + ",dns1=" + d1.str() + ",dns2=" + d2.str() + ")");
    return true;
  }
  void disconnect(bool off) {
    wifiSim.begun = false;
    if (off) {
      wifiSim.radioOff = true;
      // esp_wifi_stop() -> a netif statikus IP konfigja elveszik
      wifiSim.staticApplied = false;
    }
    simLog(off ? "WiFi.disconnect(true)" : "WiFi.disconnect(false)");
  }
  void persistent(bool v) { simLog(v ? "WiFi.persistent(true)" : "WiFi.persistent(false)"); }
  bool softAP(const char* n, const char* p) {
    (void)p; wifiSim.softApCount++;
    simLog(std::string("WiFi.softAP(") + n + ")"); return true;
  }
  IPAddress softAPIP() { return IPAddress(192,168,4,1); }
};
extern WiFiClass WiFi;
