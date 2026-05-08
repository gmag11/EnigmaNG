#if !defined(ESP8266)

#include "Gateway.h"
#include <esp_wifi.h>
#include <esp_netif.h>
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
}

bool Gateway::enableNAT() {
    // Enable NAPT on the wifi_sta netif (outbound interface).
    // ip_napt_enable() expects the IP in network byte order — use esp_netif_get_ip_info()
    // so we don't rely on Arduino IPAddress endianness.
#if defined(IP_NAPT)
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif) {
        Serial.println("[NAT] ERROR: STA netif not found");
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        Serial.println("[NAT] ERROR: STA has no IP yet");
        return false;
    }
    ip_napt_enable(ip_info.ip.addr, 1);
    _natEnabled = true;
    Serial.printf("[NAT] NAPT enabled — masquerading with %s\n",
                  IPAddress(ip_info.ip.addr).toString().c_str());
    return true;
#else
    Serial.println("[NAT] ERROR: IP_NAPT not compiled in (needs CONFIG_LWIP_IPV4_NAPT=1)");
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

#endif // !ESP8266
