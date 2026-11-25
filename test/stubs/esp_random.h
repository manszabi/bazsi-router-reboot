// esp_random() determinisztikus (xorshift) modellje.
#pragma once
#include <stdint.h>

uint32_t esp_random(void);
extern int g_randomCalls;
extern uint32_t g_randomState;
