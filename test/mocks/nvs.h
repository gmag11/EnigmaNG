#pragma once
// nvs.h stub for native unit test builds.
#include "esp_wifi.h"   // for esp_err_t
#include <stdint.h>

#define ESP_ERR_NVS_NOT_FOUND  0x1102

typedef uint32_t nvs_handle_t;
#define NVS_READONLY  0
#define NVS_READWRITE 1

inline esp_err_t nvs_open(const char*, int, nvs_handle_t* h) { if (h) *h = 0; return ESP_OK; }
inline void      nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_get_u8(nvs_handle_t, const char*, uint8_t* v)  { if (v) *v = 0; return ESP_OK; }
inline esp_err_t nvs_set_u8(nvs_handle_t, const char*, uint8_t)     { return ESP_OK; }
inline esp_err_t nvs_get_u32(nvs_handle_t, const char*, uint32_t* v){ if (v) *v = 0; return ESP_OK; }
inline esp_err_t nvs_set_u32(nvs_handle_t, const char*, uint32_t)   { return ESP_OK; }
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*) { return ESP_OK; }
inline esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t) { return ESP_OK; }
inline esp_err_t nvs_get_str(nvs_handle_t, const char*, char* out_str, size_t* len) {
    // Stub: no data available
    if (out_str == nullptr && len) { *len = 0; }
    return ESP_ERR_NVS_NOT_FOUND;
}
inline esp_err_t nvs_set_str(nvs_handle_t, const char*, const char*) { return ESP_OK; }
inline esp_err_t nvs_commit(nvs_handle_t)                            { return ESP_OK; }
