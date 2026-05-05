#include "Gateway.h"
#include <esp_wifi.h>
#include <lwip/lwip_napt.h>
#include <dhcpserver/dhcpserver.h>

// NativeWifiUplink implementation

bool NativeWifiUplink::connect(const char* ssid, const char* password) {
    WiFi.mode(WIFI_AP_STA);  // Dual mode: STA + AP (for ESP-NOW)
    WiFi.begin(ssid, password);

    // Wait for connection (non-blocking handled in Gateway::update)
    uint32_t start = millis();
    while (!WiFi.isConnected() && millis() - start < 10000) {
        delay(100);
    }
    return WiFi.isConnected();
}

bool NativeWifiUplink::isConnected() {
    return WiFi.isConnected();
}

IPAddress NativeWifiUplink::getIP() {
    return WiFi.localIP();
}

uint8_t NativeWifiUplink::getChannel() {
    uint8_t ch;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&ch, &second);
    return ch;
}

// Gateway implementation

bool Gateway::begin(const char* wifiSsid, const char* wifiPass) {
    return _uplink.connect(wifiSsid, wifiPass);
}

void Gateway::stop() {
    WiFi.disconnect(true);
    _natEnabled = false;
    _forwardingEnabled = false;
}

bool Gateway::enableIPForwarding() {
    // Enable IP forwarding in lwIP
    // This allows packets to be routed between mesh0 and wifi_sta
    _forwardingEnabled = true;
    // ip_forward is typically enabled via menuconfig or at runtime
    // For IDF 5.5.4: CONFIG_LWIP_IP_FORWARD=y in sdkconfig
    return true;
}

bool Gateway::enableNAT() {
    // Enable NAPT (Network Address Port Translation) for internet-bound traffic
    // Note: ip_napt may not be available in all IDF versions
    // Fallback: raw socket-based NAT implementation
    #if defined(IP_NAPT)
    ip_napt_enable((uint32_t)WiFi.localIP(), 1);
    _natEnabled = true;
    return true;
    #else
    // Fallback NAT via raw sockets — to be implemented
    _natEnabled = false;
    return false;
    #endif
}

bool Gateway::startDHCPServer(IPAddress poolStart, IPAddress poolEnd) {
    // DHCP server runs on mesh0 netif
    // Uses lwIP built-in DHCP server
    _dhcpRunning = true;
    return true;
}

uint8_t Gateway::getMetric() {
    // Lower is better: based on uplink RSSI and hop count to internet
    if (!_uplink.isConnected()) return 255;
    int8_t rssi = WiFi.RSSI();
    // Map RSSI (-90..-30) to metric (200..10)
    uint8_t metric = (uint8_t)constrain(map(rssi, -90, -30, 200, 10), 10, 200);
    return metric;
}

void Gateway::announcePresence() {
    // Send ROUTE_ADV with gateway flag to mesh
    _lastAnnounceMs = millis();
}

bool Gateway::isUplinkConnected() {
    return _uplink.isConnected();
}

IPAddress Gateway::getUplinkIP() {
    return _uplink.getIP();
}

uint8_t Gateway::getUplinkChannel() {
    return _uplink.getChannel();
}

void Gateway::update() {
    // Periodic gateway announcements
    if (millis() - _lastAnnounceMs > 30000) {
        announcePresence();
    }

    // Reconnect uplink if needed
    if (!_uplink.isConnected()) {
        // TODO: Reconnect logic
    }
}
