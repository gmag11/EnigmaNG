#pragma once
// esp_wifi.h stub for native unit test builds.
#include <stdint.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK  0
#define ESP_FAIL -1

typedef enum { WIFI_SECOND_CHAN_NONE = 0, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;

inline int esp_wifi_set_max_tx_power(int8_t) { return ESP_OK; }
inline int esp_wifi_get_channel(uint8_t* primary, wifi_second_chan_t* second) {
    if (primary) *primary = 1;
    if (second)  *second  = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
}
inline int esp_wifi_set_channel(uint8_t, wifi_second_chan_t) { return ESP_OK; }

// Random number generation (from esp_random.h in IDF)
inline void esp_fill_random(void* buf, size_t len) { memset(buf, 0, len); }
inline uint32_t esp_random() { return 0; }
