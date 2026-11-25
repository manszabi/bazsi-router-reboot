#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <string>

// Cimenkent allithato elerhetoseg: enelkul nem lehetne megkulonboztetni azt,
// hogy az INTERNET nem megy, attol, hogy a sajat gateway sem valaszol.
struct PingSim {
  bool ok = true;                            // alapertelmezes minden cimre
  int calls = 0;
  std::string lastTarget;
  std::vector<std::string> targets;   // a pingelt cimek sorrendje
  std::map<std::string, bool> perTarget;     // cim -> elerheto-e
  // A valodi ping a valaszig vagy a timeoutig BLOKKOL, es kozben senki nem
  // eteti a watchdogot. Enelkul a harness nem tudna kimutatni egy tul hosszu
  // etetes nelkuli szakaszt.
  uint32_t okMs   = 50;                      // sikeres ping valaszideje
  uint32_t failMs = 1000;                    // sikertelen: a teljes timeout
  // Az ESPping 1.0.5 hibaja: ha a ping_send() el sem kuldte a csomagot
  // (mem_malloc hiba), a transmitted 0 marad, igy _errors = 0 es
  // _success = count - 0 -> a ping() IGAZAT ad valasz nelkul. Ilyenkor az
  // averageTime() 0 (nem volt merheto valasz).
  bool phantomSuccess = false;
};
extern PingSim pingSim;

class PingClass {
public:
  // A valodi ESPping szignaturaja: bool ping(IPAddress dest, int16_t count = 5)
  bool ping(IPAddress t, int16_t n = 5) {
    (void)n;
    pingSim.calls++;
    pingSim.lastTarget = t.str();
    pingSim.targets.push_back(pingSim.lastTarget);
    auto it = pingSim.perTarget.find(pingSim.lastTarget);
    const bool ok = it != pingSim.perTarget.end() ? it->second : pingSim.ok;
    const uint32_t eltelt = ok ? pingSim.okMs : pingSim.failMs;
    g_millis += eltelt;                 // blokkol, etetes nelkul
    wdtAdvanceTime(eltelt);             // ...es ez az ido a meresbe is beleszamit
    if (pingSim.phantomSuccess) { avg_ = 0.0f; return true; }
    avg_ = ok ? (float)pingSim.okMs : 0.0f;
    return ok;
  }
  // ESPping.h: float averageTime() - az atlagos valaszido ms-ban.
  float averageTime() { return avg_; }
private:
  float avg_ = 0.0f;
};
extern PingClass Ping;
