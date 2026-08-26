#pragma once
#include <Arduino.h>
struct PingSim { bool ok = true; int calls = 0; };
extern PingSim pingSim;
class PingClass {
public:
  bool ping(IPAddress t, int8_t n = 5) { (void)t;(void)n; pingSim.calls++; g_millis += 100; return pingSim.ok; }
};
extern PingClass Ping;
