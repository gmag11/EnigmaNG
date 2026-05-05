#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

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
    CryptoKeys _keys;
    Onboarding _onboarding;
    WebUI _webUI;

    // Sequence counter
    uint16_t _seqCounter = 0;
    uint8_t _epoch = 0;

    // Callbacks
    MeshNodeCallback _onJoinCb = nullptr;
    MeshNodeCallback _onLeaveCb = nullptr;

    // Handshake management (max 4 concurrent)
    static constexpr size_t MAX_HANDSHAKES = 4;
    HandshakeContext _handshakes[MAX_HANDSHAKES] = {};

    // Timing
    uint32_t _lastRouteAdvMs = 0;
    uint32_t _lastBeaconMs = 0;
    uint32_t _lastPeerCheckMs = 0;

    // Internal frame handling
    static void _onFrameReceived(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);
    void _handleFrame(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);
    void _handleJoinBeacon(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchHello(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchReply(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleKeyExchConfirm(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleRouteAdv(const uint8_t* srcMac, const uint8_t* payload, size_t len);
    void _handleData(const uint8_t* srcMac, const MeshFrameHeader& hdr, const uint8_t* payload, size_t len);

    // Frame sending
    bool _sendFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
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

    // Singleton for static callback
    static MeshNetwork* _instance;
};

#endif // MESH_NETWORK_H
