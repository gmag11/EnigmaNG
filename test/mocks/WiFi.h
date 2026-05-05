#pragma once
// WiFi.h stub for native unit test builds.
#include "Arduino.h"
#include "IPAddress.h"
#include <functional>

typedef enum { WIFI_AP_STA = 3, WIFI_STA = 1, WIFI_AP = 2, WIFI_OFF = 0 } wifi_mode_t;

// Arduino ESP32 3.x event types (minimal stubs for MeshNetwork.cpp)
typedef enum {
    ARDUINO_EVENT_WIFI_AP_START   = 1,
    ARDUINO_EVENT_WIFI_STA_GOT_IP = 2,
    ARDUINO_EVENT_MAX
} arduino_event_id_t;

struct ip4_addr_stub { uint32_t addr; };
struct ip_info_stub  { ip4_addr_stub ip; ip4_addr_stub gw; ip4_addr_stub netmask; };
struct got_ip_stub   { ip_info_stub ip_info; };
union arduino_event_info_t {
    got_ip_stub got_ip;
    int         _dummy;
    arduino_event_info_t() : _dummy(0) {}
};

struct WiFiClass {
    bool begin(const char*, const char*) { return false; }
    bool isConnected() { return false; }
    IPAddress localIP() { return {}; }
    void disconnect(bool = false) {}
    int  status() { return 0; }
    int8_t RSSI() { return -90; }
    bool mode(wifi_mode_t) { return true; }
    bool softAP(const char*, const char*, uint8_t = 1) { return true; }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
    IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
    void softAPdisconnect(bool = false) {}

    // Event handler stub — accepts a lambda with (arduino_event_id_t, arduino_event_info_t)
    template<typename F>
    void onEvent(F&&, arduino_event_id_t = ARDUINO_EVENT_MAX) {}
};
inline WiFiClass WiFi;
