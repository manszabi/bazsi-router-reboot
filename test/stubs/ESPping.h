#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <string>

struct PingSim {
  bool ok = true;
  int calls = 0;
  std::string lastTarget;
  std::vector<std::string> targets;
  std::map<std::string, bool> perTarget;

  uint32_t okMs   = 50;
  uint32_t failMs = 1000;

  bool phantomSuccess = false;
};
extern PingSim pingSim;

class PingClass {
public:

  bool ping(IPAddress t, int16_t n = 5) {
    (void)n;
    pingSim.calls++;
    pingSim.lastTarget = t.str();
    pingSim.targets.push_back(pingSim.lastTarget);
    auto it = pingSim.perTarget.find(pingSim.lastTarget);
    const bool ok = it != pingSim.perTarget.end() ? it->second : pingSim.ok;
    const uint32_t eltelt = ok ? pingSim.okMs : pingSim.failMs;
    g_millis += eltelt;
    wdtAdvanceTime(eltelt);
    if (pingSim.phantomSuccess) { avg_ = 0.0f; return true; }
    avg_ = ok ? (float)pingSim.okMs : 0.0f;
    return ok;
  }

  float averageTime() { return avg_; }
private:
  float avg_ = 0.0f;
};
extern PingClass Ping;
