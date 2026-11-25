#pragma once

#include <cstdint>
#include <set>

typedef int gpio_num_t;

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif

int gpio_hold_en(gpio_num_t pin);
int gpio_hold_dis(gpio_num_t pin);
void gpio_deep_sleep_hold_en();
void gpio_deep_sleep_hold_dis();

extern std::set<int> g_heldPins;
extern bool g_deepSleepHoldEnabled;
extern bool g_gpioHoldFails;
