#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <IPAddress.h>
#include <vector>
#include <utility>

enum MeshMode : uint8_t {
    MESH_NODE    = 0,   // Standard node with relay enabled
    MESH_GATEWAY = 1,   // WiFi uplink + AP provisioning
    MESH_BATTERY = 2    // No relay, cyclic deep sleep
};

typedef void (*MeshNodeCallback)(const uint8_t* mac, IPAddress ip);
typedef void (*MeshTimeCallback)(time_t meshTime);

class MeshNetwork {
public:
    // Initialization
    bool begin(const char* psk, MeshMode mode = MESH_NODE);
    bool begin(const char* psk, IPAddress staticIP, MeshMode mode = MESH_NODE);

    // Configuration
    void setRelayEnabled(bool enabled);
    void setBatteryMode(bool enabled, uint32_t sleepIntervalSec);
    void setRssiThreshold(int8_t connectDbm, int8_t disconnectDbm);
    void setKeyRotationInterval(uint32_t seconds);
    void setMaxRoutes(uint16_t max);

    // State
    bool      isConnected();
    bool      isGateway();
    int       getNodeCount();
    int8_t    getRssiTo(const uint8_t* mac);
    int8_t    getRssiFromGateway();
    IPAddress getLocalIP();

    // Transparent IP integration
    WiFiClient& getClient();

    // Callbacks
    void onNodeJoin(MeshNodeCallback cb);
    void onNodeLeave(MeshNodeCallback cb);

    // Gateway-only
    bool startWebServer(uint16_t port = 80);
    bool startPrometheus(uint16_t port = 9090);
    void setMqttBroker(const char* host, uint16_t port);
    bool setStaticIPTable(const std::vector<std::pair<String, IPAddress>>& table);

    // Time synchronization
    time_t getMeshTime();
    void   onTimeSync(MeshTimeCallback cb);

    // Control
    void loop();
    void shutdown();

private:
    MeshMode _mode = MESH_NODE;
    bool _connected = false;
    bool _relayEnabled = true;
};

#endif // MESH_NETWORK_H
