// A WiFi és a kliens-stream modellje (core 4.0.0 viselkedés szerint; a stubcheck igazolja).
#pragma once
#include <Arduino.h>
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WL_NO_SSID_AVAIL 1
#define WL_CONNECT_FAILED 4
typedef int wl_status_t;
#define WIFI_STA 1
#define WIFI_AP 2

extern bool g_httpStall;

class WiFiClient : public Stream {
public:
  virtual bool connected() { return g_httpStall ? true : (pos_ < data_.size()); }
  int read() override { return pos_ < data_.size() ? (unsigned char)data_[pos_++] : -1; }
  int available() override { return (int)(data_.size() - pos_); }
  void setData(const std::string& d) { data_ = d; pos_ = 0; }
private:
  std::string data_; size_t pos_ = 0;
};

struct WifiSim {
  bool willConnect = false;

  uint32_t availableFrom = 0;

  int failStatus = WL_DISCONNECTED;

  bool authFail = false;
  uint32_t latencyMs = 500;
  bool begun = false;
  uint32_t beginAt = 0;
  int mode = 0;
  bool radioOff = true;

  bool staticApplied = false;
  IPAddress cfgIp, cfgGw, cfgDns1, cfgDns2;
  int beginCount = 0;
  int configCount = 0;
  bool configFails = false;
  int softApCount = 0;
  std::string apPass;
  std::string lastPass, lastSsid;

  bool sleepEnabled = true;

  IPAddress dhcpGw = IPAddress(192, 168, 1, 1);

  IPAddress staDns[3];
  int dnsSetCount = 0;
  void reset() { *this = WifiSim(); }
};
extern WifiSim wifiSim;

class STAClassStub {
public:

  bool dnsIP(uint8_t dns_no, IPAddress ip) {
    if (dns_no >= 3) return false;
    wifiSim.staDns[dns_no] = ip; wifiSim.dnsSetCount++;
    simLog("STA.dnsIP(" + std::to_string(dns_no) + "," + ip.str() + ")");
    return true;
  }
};

class WiFiClass {
public:
  STAClassStub STA;

  bool setSleep(bool enabled) {
    wifiSim.sleepEnabled = enabled;
    simLog(enabled ? "WiFi.setSleep(true)" : "WiFi.setSleep(false)");
    return true;
  }

  void mode(int m) {
    wifiSim.mode = m;
    simLog(m == WIFI_AP ? "WiFi.mode(AP)" : "WiFi.mode(STA)");
    if (m == WIFI_AP) { wifiSim.begun = false; }
  }
  void begin(const char* s, const char* p) {
    wifiSim.lastPass = p ? p : "";
    wifiSim.lastSsid = s ? s : "";
    wifiSim.begun = true; wifiSim.beginAt = g_millis; wifiSim.radioOff = false;
    wifiSim.beginCount++;
    simLog(std::string("WiFi.begin(") + (s ? s : "") + ")");
  }

  wl_status_t failNow() {
    if (wifiSim.authFail) {
      return wifiSim.beginCount >= 2 ? WL_CONNECT_FAILED : WL_DISCONNECTED;
    }
    return (wl_status_t)wifiSim.failStatus;
  }
  wl_status_t status() {
    if (!wifiSim.begun || !wifiSim.willConnect) return failNow();
    if (g_millis < wifiSim.availableFrom) return failNow();

    const uint32_t from = wifiSim.beginAt > wifiSim.availableFrom
                          ? wifiSim.beginAt : wifiSim.availableFrom;
    return (g_millis - from) >= wifiSim.latencyMs ? WL_CONNECTED : failNow();
  }
  // NetworkInterface::gatewayIP(): a netif atjaroja, kapcsolat nelkul 0.0.0.0.
  IPAddress gatewayIP() {
    if (status() != WL_CONNECTED) return IPAddress();
    return wifiSim.staticApplied ? wifiSim.cfgGw : wifiSim.dhcpGw;
  }
  String SSID() { return String("TestNet"); }
  IPAddress localIP() { return wifiSim.staticApplied ? wifiSim.cfgIp : IPAddress(192,168,1,77); }
  int RSSI() { return -50; }
  bool config(IPAddress ip, IPAddress gw, IPAddress sn, IPAddress d1 = IPAddress(), IPAddress d2 = IPAddress()) {
    (void)sn;

    if (wifiSim.configFails) { wifiSim.configCount++; simLog("WiFi.config_FAIL"); return false; }
    wifiSim.staticApplied = true; wifiSim.configCount++;
    wifiSim.cfgIp = ip; wifiSim.cfgGw = gw; wifiSim.cfgDns1 = d1; wifiSim.cfgDns2 = d2;
    simLog("WiFi.config(ip=" + ip.str() + ",gw=" + gw.str() + ",dns1=" + d1.str() + ",dns2=" + d2.str() + ")");
    return true;
  }
  void disconnect(bool off) {
    wifiSim.begun = false;
    if (off) {
      wifiSim.radioOff = true;

      wifiSim.staticApplied = false;
    }
    simLog(off ? "WiFi.disconnect(true)" : "WiFi.disconnect(false)");
  }
  void persistent(bool v) { simLog(v ? "WiFi.persistent(true)" : "WiFi.persistent(false)"); }
  bool softAP(const char* n, const char* p) {
    wifiSim.apPass = p ? p : "";

    if (!wifiSim.apPass.empty() && wifiSim.apPass.size() < 8) {
      simLog("softAP_FAIL_short_passphrase");
      return false;
    }
    wifiSim.softApCount++;
    simLog(std::string("WiFi.softAP(") + n + ")"); return true;
  }
  IPAddress softAPIP() { return IPAddress(192,168,4,1); }
};
extern WiFiClass WiFi;
