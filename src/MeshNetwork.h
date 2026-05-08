#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

#if !defined(ESP8266)

#include <Arduino.h>
#include <WiFiClient.h>
#include <IPAddress.h>
#include <vector>
#include <utility>

#include "PhysicalLayer.h"
#include "LinkLayer.h"
#include "Crypto.h"
#include "PeerManager.h"
#include "Router.h"
#include "Fragmentation.h"
#include "NetifDriver.h"
#include "Onboarding.h"
#include "WebUI.h"

enum MeshMode : uint8_t {
    MESH_NODE    = 0,   // Standard node with relay enabled
    MESH_GATEWAY = 1,   // WiFi uplink + AP provisioning
    MESH_BATTERY = 2    // No relay, cyclic deep sleep
};

typedef void (*MeshNodeCallback)(const uint8_t* mac, IPAddress ip);
typedef void (*MeshTimeCallback)(time_t meshTime);

// Handshake states for ECDH key exchange
enum class HandshakeState : uint8_t {
    IDLE = 0,
    HELLO_SENT,
    REPLY_SENT,
    CONFIRMED
};

struct HandshakeContext {
    uint8_t  peerMac[6];
    uint8_t  localPub[32];
    uint8_t  localPriv[32];
    uint8_t  peerPub[32];
    uint8_t  nonce[8];
    uint8_t  peerNonce[8];
    HandshakeState state;
    uint32_t startedAt;
};

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
    void setChannel(uint8_t channel);

    // State
    bool      isConnected();
    bool      isGateway();
    int       getNodeCount();
    int8_t    getRssiTo(const uint8_t* mac);
    int8_t    getRssiFromGateway();
    IPAddress getLocalIP();
    uint8_t   getChannel();
    const uint8_t* getMAC();

    // Transparent IP integration
    WiFiClient& getClient();

    // Data send (auto-fragments if > MTU)
    bool sendData(const uint8_t* dstMac, const uint8_t* data, size_t len);

    // Callbacks
    void onNodeJoin(MeshNodeCallback cb);
    void onNodeLeave(MeshNodeCallback cb);

    // Gateway-only
    bool startWebServer(uint16_t port = 80);
    bool startPrometheus(uint16_t port = 9090);
    bool connectUplink(const char* ssid, const char* password);
    void setMqttBroker(const char* host, uint16_t port);
    bool setStaticIPTable(const std::vector<std::pair<String, IPAddress>>& table);

    // Time synchronization
    time_t getMeshTime();
    void   onTimeSync(MeshTimeCallback cb);

    // Control
    void loop();
    void shutdown();

    // Access to subsystems (for WebUI etc.)
    PeerManager& getPeerManager() { return _peerMgr; }
    Router& getRouter() { return _router; }
    MeshPhysicalLayer& getPhysical() { return _phy; }

private:
    MeshMode _mode = MESH_NODE;
    bool _connected = false;
    bool _relayEnabled = true;
    uint8_t _channel = 6;
    uint8_t _localMac[6] = {};
    IPAddress _localIP;
    char _psk[64] = {};

    // Subsystems
    MeshPhysicalLayer _phy;
    PeerManager _peerMgr;
    Router _router;
    Fragmentation _frag;
    NetifDriver _netifDrv;
    CryptoKeys _keys;
    Onboarding _onboarding;
    WebUI _webUI;

    // Sequence counter
    uint16_t _seqCounter = 0;
    uint8_t _epoch = 0;

    // Key rotation
    uint32_t _keyRotationIntervalMs = 0;  // 0 = disabled
    uint32_t _lastEpochRotationMs = 0;

    // KEY_NACK: buffer of 1 rejected frame per peer (for retransmission after renegotiation)
    struct NackBuffer {
        uint8_t  peerMac[6];
        uint8_t  frame[250];
        size_t   frameLen;
        bool     valid;
    };
    NackBuffer _nackBuf = {};

    // Callbacks
    MeshNodeCallback _onJoinCb = nullptr;
    MeshNodeCallback _onLeaveCb = nullptr;
    MeshTimeCallback _onTimeSyncCb = nullptr;

    // Handshake management (max 4 concurrent)
    static constexpr size_t MAX_HANDSHAKES = 4;
    HandshakeContext _handshakes[MAX_HANDSHAKES] = {};

    // Timing
    uint32_t _lastRouteAdvMs = 0;
    uint32_t _lastBeaconMs = 0;
    uint32_t _lastPeerCheckMs = 0;
    uint32_t _sleepIntervalSec = 0;  // For MESH_BATTERY mode, announced in JOIN_BEACON

    // Internal frame handling
    static void _onFrameReceived(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);
    void _handleFrame(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);
    void _handleJoinBeacon(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchHello(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchReply(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchConfirm(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleRouteAdv(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleRouteWithdraw(const uint8_t* payload, size_t len);
    void _handleKeyNack(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleArpQuery(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleArpReply(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleData(const uint8_t* srcMac, const MeshFrameHeader& hdr, const uint8_t* payload, size_t len);

    // Frame sending
    // _sendFrame: dstMac is both the mesh header end-to-end dst AND the physical ESP-NOW peer
    bool _sendFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
                    const uint8_t* payload, size_t payloadLen);
    // _sendFrameVia: finalDstMac goes in the mesh header, nextHopMac is the physical ESP-NOW peer
    bool _sendFrameVia(const uint8_t* finalDstMac, const uint8_t* nextHopMac,
                       FrameType type, Protocol proto,
                       const uint8_t* payload, size_t payloadLen);
    bool _sendBroadcastFrame(FrameType type, Protocol proto,
                             const uint8_t* payload, size_t payloadLen);

    // Handshake
    HandshakeContext* _findHandshake(const uint8_t* mac);
    HandshakeContext* _allocHandshake(const uint8_t* mac);
    void _freeHandshake(const uint8_t* mac);
    void _initiateHandshake(const uint8_t* peerMac);

    // Deferred web server start (triggered by WiFi AP_START event)
    uint16_t _pendingWebPort = 0;

    // Periodic tasks
    void _sendRouteAdv();
    void _sendJoinBeacon();
    void _checkPeerTimeouts();
    void _rotateEpoch();
    void _sendGratuitousArp();

    // Netif TX callback (lwIP → mesh)
    static bool _netifTxCallback(const uint8_t* data, size_t len, void* ctx);

    // Singleton for static callback
    static MeshNetwork* _instance;
};

#endif // !ESP8266
#endif // MESH_NETWORK_H
