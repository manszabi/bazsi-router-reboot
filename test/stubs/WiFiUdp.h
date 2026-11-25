// NetworkUDP modell DNS-hez: szerverenként állítható válasz, nem blokkoló fogadás (core 4.0.0).
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <map>
#include <string>
#include <vector>

enum DnsMode { DNS_OK = 0, DNS_SILENT, DNS_SERVFAIL, DNS_NXDOMAIN, DNS_BAD_ID, DNS_WRONG_SRC, DNS_SHORT };

struct DnsSim {
  DnsMode defaultMode = DNS_OK;
  std::map<std::string, DnsMode> perServer;
  uint32_t latencyMs = 20;
  bool beginOk = true;
  int queries = 0;
  std::vector<std::string> servers;
  std::vector<std::string> names;
};
extern DnsSim dnsSim;

class WiFiUDP {
public:
  uint8_t begin(uint16_t) { open_ = dnsSim.beginOk; return open_ ? 1 : 0; }
  int beginPacket(IPAddress ip, uint16_t port) { dst_ = ip; dstPort_ = port; tx_.clear(); return open_ ? 1 : 0; }
  size_t write(const uint8_t* b, size_t n) { tx_.insert(tx_.end(), b, b + n); return n; }
  int endPacket() {
    if (!open_ || tx_.size() < 12) return 0;
    dnsSim.queries++;
    dnsSim.servers.push_back(dst_.str());
    dnsSim.names.push_back(qname());
    auto it = dnsSim.perServer.find(dst_.str());
    const DnsMode m = it != dnsSim.perServer.end() ? it->second : dnsSim.defaultMode;
    if (m == DNS_SILENT) return 1;
    std::vector<uint8_t> r(tx_);
    r[2] = 0x81; r[3] = 0x80;
    if (m == DNS_SERVFAIL) r[3] |= 2;
    if (m == DNS_NXDOMAIN) r[3] |= 3;
    if (m == DNS_BAD_ID) r[1] ^= 0x5A;
    if (m == DNS_SHORT) r.resize(8);
    rx_ = r;
    from_ = (m == DNS_WRONG_SRC) ? IPAddress(10, 9, 9, 9) : dst_;
    readyAt_ = g_millis + dnsSim.latencyMs;
    pending_ = true;
    return 1;
  }
  int parsePacket() {
    if (!pending_ || g_millis < readyAt_) return 0;
    pending_ = false; have_ = true; pos_ = 0;
    return (int)rx_.size();
  }
  int read(uint8_t* b, size_t n) {
    if (!have_) return 0;
    size_t k = 0;
    while (k < n && pos_ < rx_.size()) b[k++] = rx_[pos_++];
    return (int)k;
  }
  void flush() { have_ = false; }
  void stop() { open_ = false; pending_ = false; have_ = false; }
  IPAddress remoteIP() { return from_; }
  uint16_t remotePort() { return 53; }
private:
  std::string qname() const {
    std::string s; size_t i = 12;
    while (i < tx_.size() && tx_[i]) { const size_t l = tx_[i++]; if (!s.empty()) s += '.';
      for (size_t j = 0; j < l && i < tx_.size(); j++) s += (char)tx_[i++]; }
    return s;
  }
  bool open_ = false, pending_ = false, have_ = false;
  IPAddress dst_, from_;
  uint16_t dstPort_ = 0;
  std::vector<uint8_t> tx_, rx_;
  size_t pos_ = 0;
  uint32_t readyAt_ = 0;
};
