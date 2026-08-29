#pragma once
// Az IDF driver/gpio.h minimalis masa: csak a hold API, amit a sketch hasznal.
// A valodi fuggvenyek esp_err_t-t adnak; itt int, az ertekek ugyanazok.
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

// A hold allapota a valosaghoz huen TULELI az ebredest (reset): a coldBoot()
// csak valodi aramtalanitasnal (deepSleepWake=false) torli.
extern std::set<int> g_heldPins;      // gpio_hold_en-nel rogzitett labak
extern bool g_deepSleepHoldEnabled;   // gpio_deep_sleep_hold_en aktiv-e
extern bool g_gpioHoldFails;          // a gpio_hold_en() adjon hibat
