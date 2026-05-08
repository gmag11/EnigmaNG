#ifndef MESH_GATEWAY_H
#define MESH_GATEWAY_H

#if !defined(ESP8266)

#include <Arduino.h>
#include <WiFi.h>
#include <IPAddress.h>

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

    // IP forwarding between mesh0 and wifi_sta
    bool enableIPForwarding();

    // NAT masquerade for internet traffic
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
    bool _forwardingEnabled = false;
    bool _dhcpRunning = false;
    uint32_t _lastAnnounceMs = 0;
};

#endif // !ESP8266
#endif // MESH_GATEWAY_H
