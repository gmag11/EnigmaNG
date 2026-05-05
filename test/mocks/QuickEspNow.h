#pragma once
// QuickEspNow.h stub for native unit test builds.
#include "esp_wifi.h"   // pulls in esp_err_t and esp_wifi_set_max_tx_power
#include <stdint.h>

// Match the actual QuickEspNow callback signature used in PhysicalLayer.cpp
typedef void (*ESPNOW_RECV_CB_t)(const uint8_t*, const uint8_t*, int, signed int, bool);

struct QuickESPNowClass {
    void onDataRcvd(ESPNOW_RECV_CB_t) {}
    bool begin(uint8_t, bool = false) { return false; }
    int  send(const uint8_t*, const uint8_t*, int) { return -1; }
    void stop() {}
};
inline QuickESPNowClass quickEspNow;
