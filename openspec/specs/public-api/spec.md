# Spec: API Pública de la Librería

**Referencia:** §14 de EnigmaNG Specs v2.md

## Propósito

Exponer la funcionalidad completa de EnigmaNG a través de una API C++ limpia, compatible con el ecosistema Arduino (PubSubClient, HTTPClient, etc.) y neutral para el usuario final.

## Clase principal: `MeshNetwork`

```cpp
class MeshNetwork {
public:
    // Inicialización
    bool begin(const char* psk, MeshMode mode = MESH_NODE);
    bool begin(const char* psk, IPAddress staticIP, MeshMode mode = MESH_NODE);

    // Configuración
    void setRelayEnabled(bool enabled);
    void setBatteryMode(bool enabled, uint32_t sleepIntervalSec);
    void setRssiThreshold(int8_t connectDbm, int8_t disconnectDbm);
    void setKeyRotationInterval(uint32_t seconds);
    void setMaxRoutes(uint16_t max);

    // Estado
    bool      isConnected();
    bool      isGateway();
    int       getNodeCount();
    int8_t    getRssiTo(const uint8_t* mac);
    int8_t    getRssiFromGateway();
    IPAddress getLocalIP();

    // Integración IP transparente
    // Devuelve un Client compatible con PubSubClient, HTTPClient, etc.
    // El stack lwIP gestiona el routing sobre la mesh automáticamente.
    WiFiClient& getClient();

    // Callbacks
    void onNodeJoin(MeshNodeCallback cb);
    void onNodeLeave(MeshNodeCallback cb);

    // Gateway-only
    bool startWebServer(uint16_t port = 80);
    bool startPrometheus(uint16_t port = 9090);
    void setMqttBroker(const char* host, uint16_t port);
    bool setStaticIPTable(const std::vector<std::pair<String, IPAddress>>& table);

    // Sincronización de tiempo
    time_t getMeshTime();
    void   onTimeSync(MeshTimeCallback cb);

    // Control
    void loop();
    void shutdown();
};

enum MeshMode : uint8_t {
    MESH_NODE    = 0,   // Nodo estándar con relay habilitado
    MESH_GATEWAY = 1,   // Con WiFi uplink + AP provisioning
    MESH_BATTERY = 2    // Sin relay, deep sleep cíclico
};
```

## Principio de integración IP transparente

`getClient()` devuelve un `WiFiClient` wrapeado sobre un socket lwIP en el netif `mesh0`. Esto permite:

```cpp
MeshNetwork mesh;
WiFiClient& client = mesh.getClient();
PubSubClient mqttClient(client);  // Funciona sin modificaciones
HTTPClient http;
http.begin(client, "http://10.200.0.1/api"); // Funciona sin modificaciones
```

lwIP gestiona el routing automáticamente: destinos en `10.200.0.0/16` van por `mesh0`, el resto por `wifi_sta` (en gateways).

## Estructura de directorios del repositorio

```
EnigmaNG/
  src/                       ← Código principal (compatible IDF + Arduino)
    MeshNetwork.h/.cpp        ← API pública
    PhysicalLayer.h/.cpp      ← Wrapper QuickESPNow
    LinkLayer.h/.cpp          ← Serialización de frames
    Crypto.h/.cpp             ← ECDH + AES-GCM + HKDF
    PeerManager.h/.cpp        ← Hash table de peers
    Router.h/.cpp             ← DVR + RouteTable + SeenFrameCache
    NetifDriver.h/.cpp        ← esp_netif virtual mesh0
    Onboarding.h/.cpp         ← AP provisioning + canal discovery
    BatteryNode.h/.cpp        ← Ciclo LoRaWAN-A + Parent buffer
    Gateway.h/.cpp            ← WiFi uplink + routing LAN+NAT
    WebUI.h/.cpp              ← esp_http_server + Digest Auth
    ServiceDiscovery.h/.cpp   ← Protocolo propio + mDNS bridge
  arduino/                   ← Wrapper Arduino específico
    MeshNetwork.h             ← Hereda/wrappea src/MeshNetwork
  idf_component/             ← Para proyectos IDF nativos
    CMakeLists.txt
    idf_component.yml
  examples/
    arduino/
      BasicNode/
      GatewaySingleChip/
      BatteryNode/
    idf/
      gateway_hosted/         ← Gateway dual-board (IDF only)
  test/                      ← Tests unitarios (Unity framework)
  library.json               ← Metadatos Arduino Library Manager
```

## Criterio de aceptación

- Test: `PubSubClient` con `getClient()` conecta a broker MQTT en la LAN a través de la mesh. Publica y recibe mensajes.
- Test: `HTTPClient` hace GET a servidor HTTP en la LAN a través del gateway.
- Test: `begin()` con PSK incorrecta no conecta a la mesh.
- Test: `onNodeJoin` callback llamado cuando un nuevo nodo se une.
- Test: `loop()` debe llamarse regularmente; verificar que sin `loop()` la mesh no recibe frames.
