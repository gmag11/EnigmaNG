#pragma once
// WiFi.h stub for native unit test builds.
#include "Arduino.h"
#include "IPAddress.h"

typedef enum { WIFI_AP_STA = 3, WIFI_STA = 1, WIFI_AP = 2, WIFI_OFF = 0 } wifi_mode_t;

struct WiFiClass {
    bool begin(const char*, const char*) { return false; }
    bool isConnected() { return false; }
    IPAddress localIP() { return {}; }
    void disconnect(bool = false) {}
    int  status() { return 0; }
    int8_t RSSI() { return -90; }
    void mode(wifi_mode_t) {}
    void softAP(const char*, const char*, uint8_t = 1) {}
    void softAPdisconnect(bool = false) {}
};
inline WiFiClass WiFi;
