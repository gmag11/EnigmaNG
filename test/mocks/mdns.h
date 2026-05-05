#pragma once
// mdns.h stub for native unit test builds.
#include "esp_wifi.h"

inline esp_err_t mdns_init()                           { return ESP_OK; }
inline esp_err_t mdns_hostname_set(const char*)        { return ESP_OK; }
inline esp_err_t mdns_service_add(const char*, const char*, const char*, uint16_t, void*, int) { return ESP_OK; }
inline esp_err_t mdns_service_remove(const char*, const char*) { return ESP_OK; }
inline void      mdns_free()                           {}
