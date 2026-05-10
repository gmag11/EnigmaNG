# Spec: Library Public API

**Reference:** §14 of EnigmaNG Specs v2.md

## Purpose

Expose the full EnigmaNG functionality through a clean C++ API, compatible with the Arduino ecosystem (PubSubClient, HTTPClient, etc.) and transparent to end users.

## Main class: `MeshNetwork`

```cpp
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

    // Status
    bool      isConnected();
    bool      isGateway();
    int       getNodeCount();
    int8_t    getRssiTo(const uint8_t* mac);
    int8_t    getRssiFromGateway();
    IPAddress getLocalIP();

    // Transparent IP integration
    // Returns a Client compatible with PubSubClient, HTTPClient, etc.
    // The lwIP stack handles routing over the mesh automatically.
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
};

enum MeshMode : uint8_t {
    MESH_NODE    = 0,   // Standard node with relay enabled
    MESH_GATEWAY = 1,   // With WiFi uplink + AP provisioning
    MESH_BATTERY = 2    // No relay, cyclic deep sleep
};
```

## Principle of transparent IP integration

`getClient()` returns a wrapped `WiFiClient` around an lwIP socket on netif `mesh0`. This allows:

```cpp
MeshNetwork mesh;
WiFiClient& client = mesh.getClient();
PubSubClient mqttClient(client);  // Works without changes
HTTPClient http;
http.begin(client, "http://10.200.0.1/api"); // Works without changes
```

lwIP handles routing automatically: destinations in `10.200.0.0/16` go via `mesh0`, the rest via `wifi_sta` (on gateways).

## Repository directory structure

```
EnigmaNG/
  src/                       ← Main code (IDF + Arduino compatible)
    MeshNetwork.h/.cpp        ← Public API
    PhysicalLayer.h/.cpp      ← QuickESPNow wrapper
    LinkLayer.h/.cpp          ← Frame serialization
    Crypto.h/.cpp             ← ECDH + AES-GCM + HKDF
    PeerManager.h/.cpp        ← Peer hash table
    Router.h/.cpp             ← DVR + RouteTable + SeenFrameCache
    NetifDriver.h/.cpp        ← esp_netif virtual mesh0
    Onboarding.h/.cpp         ← AP provisioning + channel discovery
    BatteryNode.h/.cpp        ← LoRaWAN-A cycle + Parent buffer
    Gateway.h/.cpp            ← WiFi uplink + LAN+NAT routing
    WebUI.h/.cpp              ← esp_http_server + Digest Auth
    ServiceDiscovery.h/.cpp   ← Custom protocol + mDNS bridge
  arduino/                   ← Arduino-specific wrapper
    MeshNetwork.h             ← Inherits/wraps src/MeshNetwork
  idf_component/             ← For native IDF projects
    CMakeLists.txt
    idf_component.yml
  examples/
    arduino/
      BasicNode/
      GatewaySingleChip/
      BatteryNode/
    idf/
      gateway_hosted/         ← Dual-board gateway (IDF only)
  test/                      ← Unit tests (Unity framework)
  library.json               ← Arduino Library Manager metadata
```

## Acceptance criteria

- Test: `PubSubClient` with `getClient()` connects to an MQTT broker on the LAN via the mesh. Publishes and receives messages.
- Test: `HTTPClient` performs a GET to an HTTP server on the LAN via the gateway.
- Test: `begin()` with incorrect PSK does not connect to the mesh.
- Test: `onNodeJoin` callback is invoked when a new node joins.
- Test: `loop()` must be called regularly; verify that without `loop()` the mesh does not receive frames.
