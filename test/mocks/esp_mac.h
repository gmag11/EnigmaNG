#pragma once
// esp_mac.h stub for native unit test builds.
#include "esp_wifi.h"
#include <stdint.h>
#include <string.h>

typedef enum { ESP_MAC_WIFI_STA = 0, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH } esp_mac_type_t;

inline esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type) {
    (void)type;
    // Return a deterministic fake MAC for tests
    static const uint8_t fake[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    memcpy(mac, fake, 6);
    return ESP_OK;
}
