#pragma once
#include <stdint.h>
// esp_hw_support/include/esp_random.h: uint32_t esp_random(void).
// A harnessben DETERMINISZTIKUS (xorshift), hogy a forgatokonyvek
// reprodukalhatok legyenek; a g_randomCalls a hivasok szamat meri.
uint32_t esp_random(void);
extern int g_randomCalls;
extern uint32_t g_randomState;
