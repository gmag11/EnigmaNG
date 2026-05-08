#ifndef MESH_NODE_8266_H
#define MESH_NODE_8266_H

#ifdef ESP8266

#include <Arduino.h>
#include <IPAddress.h>
#include "Crypto.h"
#include "LinkLayer.h"
#include "PhysicalLayer.h"
#include "meshConfig.h"

// ─── Proxy protocol types ────────────────────────────────────────────

enum class ProxyMsgType : uint8_t {
    PROXY_CONNECT      = 0x01,
    PROXY_PUBLISH      = 0x02,
    PROXY_SUBSCRIBE    = 0x03,
    PROXY_UNSUBSCRIBE  = 0x04,
    PROXY_MESSAGE      = 0x05,  // Broker → ESP8266
    PROXY_PUBACK       = 0x06,  // QoS1 confirmation
    PROXY_DISCONNECT   = 0x07,
    PROXY_DISCOVERY    = 0x08,  // Broadcast: find available proxy
    PROXY_OFFER        = 0x09   // Unicast: proxy response
};

// ─── Callbacks ───────────────────────────────────────────────────────

typedef void (*MqttMessageCallback)(const char* topic, const uint8_t* payload, size_t len);
typedef void (*MeshConnectCallback)();

// ─── MeshNode8266 ────────────────────────────────────────────────────

class MeshNode8266 {
public:
    MeshNode8266();

    // Initialization
    bool begin(const char* psk, uint8_t channel = 0);

    // MQTT-over-mesh API
    bool mqttPublish(const char* topic, const uint8_t* payload,
                     size_t len, uint8_t qos = 0, bool retain = false);
    bool mqttSubscribe(const char* topic, uint8_t qos = 0);
    bool mqttUnsubscribe(const char* topic);

    // Callbacks
    void onMqttMessage(MqttMessageCallback cb);
    void onConnected(MeshConnectCallback cb);

    // State
    bool isConnected() const;
    IPAddress getProxyIP() const;
    int8_t getProxyRSSI() const;

    // Battery / sleep
    void setSleepDuration(uint32_t seconds);

    // Main loop — must be called frequently
    void loop();

private:
    // ─── Internal state ──────────────────────────────────────────────
    enum class State : uint8_t {
        IDLE,
        SCANNING,           // Channel scan for JOIN_BEACON
        DISCOVERING,        // Sent PROXY_DISCOVERY, waiting for offers
        HANDSHAKE,          // ECDH handshake with proxy
        CONNECTED           // Ready to publish/subscribe
    };

    State _state = State::IDLE;

    // PSK and derived keys
    const char* _psk = nullptr;
    CryptoKeys  _cryptoKeys;
    uint8_t     _linkKey[MESH_KEY_SIZE] = {};
    uint8_t     _epoch = 0;
    uint16_t    _seqTx = 0;

    // Physical layer
    MeshPhysicalLayer _phy;
    uint8_t _localMac[6] = {};
    uint8_t _channel = 0;

    // Proxy info
    uint8_t _proxyMac[6] = {};
    int8_t  _proxyRssi = -127;
    IPAddress _proxyIP;

    // Broker info (from JOIN_BEACON or provisioning)
    char    _brokerHost[48] = {};
    uint16_t _brokerPort = 1883;

    // Discovery state
    struct ProxyOffer {
        uint8_t mac[6];
        int8_t  rssi;
        bool    valid;
    };
    ProxyOffer _offers[MESH8266_MAX_PROXY_OFFERS] = {};
    uint32_t   _discoveryStartMs = 0;

    // Handshake
    uint8_t _localPub[MESH_ECDH_KEY_SIZE]  = {};
    uint8_t _localPriv[MESH_ECDH_KEY_SIZE] = {};
    uint8_t _peerPub[MESH_ECDH_KEY_SIZE]   = {};
    uint8_t _nonce[8]     = {};
    uint8_t _peerNonce[8] = {};
    uint32_t _handshakeStartMs = 0;

    // Subscriptions
    struct Subscription {
        char topic[MESH8266_MAX_TOPIC_LEN];
        bool active;
    };
    Subscription _subs[MESH8266_MAX_SUBSCRIPTIONS] = {};

    // Callbacks
    MqttMessageCallback _msgCb = nullptr;
    MeshConnectCallback _connCb = nullptr;

    // Timers
    uint32_t _lastBeaconMs = 0;
    uint32_t _sleepDurationSec = 0;

    // ─── Internal methods ────────────────────────────────────────────
    void _onFrameReceived(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);
    static void _rxCallback(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);

    bool _scanForBeacon();
    void _startDiscovery();
    void _handleProxyOffer(const uint8_t* srcMac, const uint8_t* payload, size_t len, int8_t rssi);
    void _selectBestProxy();
    void _initiateHandshake();
    void _handleKeyExchReply(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchConfirm(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleJoinBeacon(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleProxyMessage(const uint8_t* payload, size_t len);
    void _handleProxyPuback(const uint8_t* payload, size_t len);

    bool _sendFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
                    const uint8_t* payload, size_t payloadLen);
    bool _sendEncryptedFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
                             const uint8_t* payload, size_t payloadLen);
    void _sendProxyConnect();
    void _resubscribeAll();
};

// Singleton-ish for static callback routing
extern MeshNode8266* _meshNode8266Instance;

#endif // ESP8266
#endif // MESH_NODE_8266_H
