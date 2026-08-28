#pragma once
#include <Arduino.h>
#include <map>
#include <string>

// Cimenkent allithato elerhetoseg: enelkul nem lehetne megkulonboztetni azt,
// hogy az INTERNET nem megy, attol, hogy a sajat gateway sem valaszol.
struct PingSim {
  bool ok = true;                            // alapertelmezes minden cimre
  int calls = 0;
  std::string lastTarget;
  std::map<std::string, bool> perTarget;     // cim -> elerheto-e
};
extern PingSim pingSim;

class PingClass {
public:
  // A valodi ESPping szignaturaja: bool ping(IPAddress dest, int16_t count = 5)
  bool ping(IPAddress t, int16_t n = 5) {
    (void)n;
    pingSim.calls++;
    pingSim.lastTarget = t.str();
    g_millis += 100;
    auto it = pingSim.perTarget.find(pingSim.lastTarget);
    return it != pingSim.perTarget.end() ? it->second : pingSim.ok;
  }
};
extern PingClass Ping;
