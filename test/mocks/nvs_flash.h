#pragma once
// nvs_flash.h stub for native unit test builds.
#include "esp_wifi.h"   // for esp_err_t

inline esp_err_t nvs_flash_init()  { return ESP_OK; }
inline esp_err_t nvs_flash_erase() { return ESP_OK; }
