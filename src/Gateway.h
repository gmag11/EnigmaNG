#ifndef MESH_GATEWAY_H
#define MESH_GATEWAY_H

#if !defined(ESP8266)

#ifndef CONFIG_LWIP_IP_FORWARD
#define CONFIG_LWIP_IP_FORWARD 1
#endif
#ifndef CONFIG_LWIP_IPV4_NAPT
#define CONFIG_LWIP_IPV4_NAPT 1
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <IPAddress.h>
#include <esp_netif.h>

// Gateway: WiFi STA + ESP-NOW dual mode
// Handles routing between mesh0 and wifi_sta, NAT, DHCP server

class MeshUplink {
public:
    virtual bool connect(const char* ssid, const char* password) = 0;
    virtual bool isConnected() = 0;
    virtual IPAddress getIP() = 0;
    virtual uint8_t getChannel() = 0;
    virtual ~MeshUplink() {}
};

class NativeWifiUplink : public MeshUplink {
public:
    bool connect(const char* ssid, const char* password) override;
    bool isConnected() override;
    IPAddress getIP() override;
    uint8_t getChannel() override;
};

class Gateway {
public:
    bool begin(const char* wifiSsid, const char* wifiPass);
    void stop();

    // NAT masquerade: always enabled after uplink connects
    // CONFIG_LWIP_IP_FORWARD and CONFIG_LWIP_IPV4_NAPT default to 1 (see top of this header)
    bool enableNAT();

    // DHCP server for mesh nodes
    bool startDHCPServer(IPAddress poolStart, IPAddress poolEnd);

    // Gateway selection metrics
    uint8_t getMetric();
    void announcePresence();

    // State
    bool isUplinkConnected();
    IPAddress getUplinkIP();
    uint8_t getUplinkChannel();

    void update();

private:
    NativeWifiUplink _uplink;
    bool _natEnabled = false;
    bool _dhcpRunning = false;
    uint32_t _lastAnnounceMs = 0;
};

#endif // !ESP8266
#endif // MESH_GATEWAY_H
