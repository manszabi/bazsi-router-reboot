#pragma once
#include <Arduino.h>
// Az IDF v5.5 esp_task_wdt.h-jának minimális mása
typedef struct {
  uint32_t timeout_ms;
  uint32_t idle_core_mask;
  bool trigger_panic;
} esp_task_wdt_config_t;

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102

esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t*);
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t*);
