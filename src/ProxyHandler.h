#ifndef MESH_PROXY_HANDLER_H
#define MESH_PROXY_HANDLER_H

#if !defined(ESP8266)

#include <Arduino.h>
#include <IPAddress.h>
#include <mqtt_client.h>
#include "Crypto.h"

// Proxy message types — must match MeshNode8266.h
enum class ProxyMsgType : uint8_t {
    PROXY_CONNECT      = 0x01,
    PROXY_PUBLISH      = 0x02,
    PROXY_SUBSCRIBE    = 0x03,
    PROXY_UNSUBSCRIBE  = 0x04,
    PROXY_MESSAGE      = 0x05,
    PROXY_PUBACK       = 0x06,
    PROXY_DISCONNECT   = 0x07,
    PROXY_DISCOVERY    = 0x08,
    PROXY_OFFER        = 0x09
};

#ifndef PROXY_MAX_CLIENTS
#define PROXY_MAX_CLIENTS 8
#endif
#ifndef PROXY_MAX_SUBS_PER_CLIENT
#define PROXY_MAX_SUBS_PER_CLIENT 4
#endif

class MeshNetwork;  // Forward declaration

// ─── ProxyHandler ────────────────────────────────────────────────────
// Handles ESP8266 proxy clients on the ESP32 gateway side.
// Translates PROXY_* frames ↔ MQTT broker connections.

class ProxyHandler {
public:
    ProxyHandler();
    ~ProxyHandler();

    // Set broker configuration
    void setBroker(const char* host, uint16_t port);
    void setBrokerAuth(const char* user, const char* password);

    // Initialize with reference to the mesh network (for sending frames)
    void begin(MeshNetwork* mesh);

    // Called periodically from MeshNetwork::loop()
    void loop();

    // Handle incoming PROXY frame from an ESP8266 (after decryption)
    void handleProxyFrame(const uint8_t* srcMac, const uint8_t* payload, size_t len);

    // Handle discovery (unencrypted broadcast)
    void handleDiscovery(const uint8_t* srcMac);

    // Get broker info for JOIN_BEACON extension
    const char* getBrokerHost() const { return _brokerHost; }
    uint16_t getBrokerPort() const { return _brokerPort; }
    bool hasBroker() const { return _brokerHost[0] != '\0'; }

private:
    // ─── Client tracking ─────────────────────────────────────────────
    struct ProxyClient {
        uint8_t mac[6];
        bool    connected;
        char    subscriptions[PROXY_MAX_SUBS_PER_CLIENT][64];
        uint8_t subCount;
    };

    ProxyClient _clients[PROXY_MAX_CLIENTS] = {};

    // ─── MQTT ────────────────────────────────────────────────────────
    esp_mqtt_client_handle_t _mqttHandle = nullptr;
    bool     _mqttConnected = false;
    char     _brokerHost[48] = {};
    uint16_t _brokerPort = 1883;
    char     _brokerUser[32] = {};
    char     _brokerPass[32] = {};

    // ─── Reference ───────────────────────────────────────────────────
    MeshNetwork* _mesh = nullptr;

    // ─── Internal methods ────────────────────────────────────────────
    ProxyClient* _findClient(const uint8_t* mac);
    ProxyClient* _addClient(const uint8_t* mac);
    void _removeClient(const uint8_t* mac);

    void _handleConnect(const uint8_t* srcMac);
    void _handleDisconnect(const uint8_t* srcMac);
    void _handlePublish(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleSubscribe(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleUnsubscribe(const uint8_t* srcMac, const uint8_t* payload, size_t len);

    static void _mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    void _onMqttMessage(char* topic, uint8_t* payload, unsigned int len);

    void _sendProxyMessage(const uint8_t* dstMac, const char* topic, const uint8_t* payload, size_t len);
    void _sendProxyPuback(const uint8_t* dstMac, uint16_t packetId);
};

#endif // !ESP8266
#endif // MESH_PROXY_HANDLER_H
