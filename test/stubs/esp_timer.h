#pragma once
#include <stdint.h>
// Az IDF esp_timer.h-jából csak ennyi kell: a boot óta eltelt mikroszekundum.
// A valódi core is expliciten includeolja ezt a fejlécet (esp32-hal-misc.c).
int64_t esp_timer_get_time();
