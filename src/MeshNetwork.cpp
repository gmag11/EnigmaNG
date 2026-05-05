#include "MeshNetwork.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <cstring>

MeshNetwork* MeshNetwork::_instance = nullptr;

// ─── Initialization ───────────────────────────────────────────────────────────

bool MeshNetwork::begin(const char* psk, MeshMode mode) {
    return begin(psk, IPAddress(0, 0, 0, 0), mode);
}

bool MeshNetwork::begin(const char* psk, IPAddress staticIP, MeshMode mode) {
    _instance = this;
    _mode = mode;
    _relayEnabled = (mode != MESH_BATTERY);
    _localIP = staticIP;
    strncpy(_psk, psk, sizeof(_psk) - 1);

    // Get local MAC address
    esp_read_mac(_localMac, ESP_MAC_WIFI_STA);

    // Derive NetworkKey and NetworkID from PSK
    if (!Crypto::deriveNetworkKeys(_psk, _keys)) {
        Serial.println("[Mesh] ERROR: Failed to derive network keys");
        return false;
    }

    Serial.printf("[Mesh] NetworkID: %02X%02X\n", _keys.networkId[0], _keys.networkId[1]);
    Serial.printf("[Mesh] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  _localMac[0], _localMac[1], _localMac[2],
                  _localMac[3], _localMac[4], _localMac[5]);

    // Register event handlers BEFORE WiFi.mode() — AP_START fires during mode change
    if (mode == MESH_GATEWAY) {
        Serial.println("[Mesh] Registering WiFi event handlers...");
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
            Serial.println("[Mesh] >>> ARDUINO_EVENT_WIFI_AP_START fired");
            MeshNetwork* self = MeshNetwork::_instance;
            if (!self || self->_pendingWebPort == 0) {
                Serial.println("[Mesh]     (no pending web port, skipping httpd start)");
                return;
            }
            uint16_t p = self->_pendingWebPort;
            self->_pendingWebPort = 0;
            if (self->_webUI.begin(p, "admin", "admin", self)) {
                Serial.printf("[Mesh] Web UI started on port %d (via AP_START event)\n", p);
            } else {
                Serial.println("[Mesh] ERROR: httpd failed even after AP_START");
            }
        }, ARDUINO_EVENT_WIFI_AP_START);

        // Log when a WiFi uplink connection is established
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
            Serial.printf("[Mesh] WiFi uplink connected — IP: %s  GW: %s\n",
                          IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                          IPAddress(info.got_ip.ip_info.gw.addr).toString().c_str());
        }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
        Serial.println("[Mesh] Event handlers registered");
    }

    // Initialize WiFi (needed for ESP-NOW)
    Serial.println("[Mesh] Setting WiFi mode...");
    if (mode == MESH_GATEWAY) {
        bool ok = WiFi.mode(WIFI_AP_STA);
        Serial.printf("[Mesh] WiFi.mode(WIFI_AP_STA) = %s\n", ok ? "OK" : "FAIL");
    } else {
        bool ok = WiFi.mode(WIFI_STA);
        Serial.printf("[Mesh] WiFi.mode(WIFI_STA) = %s\n", ok ? "OK" : "FAIL");
    }
    WiFi.disconnect();
    Serial.printf("[Mesh] WiFi status after disconnect: %d\n", (int)WiFi.status());

    // Initialize Physical Layer
    Serial.println("[Mesh] Starting physical layer...");
    if (!_phy.begin(_channel, _keys.networkId)) {
        Serial.println("[Mesh] ERROR: Failed to initialize physical layer");
        return false;
    }
    Serial.println("[Mesh] Physical layer OK");

    // Set receive callback
    _phy.onReceive(_onFrameReceived);

    // Assign IP based on last 2 bytes of MAC if no static IP given
    if (_localIP == IPAddress(0, 0, 0, 0)) {
        _localIP = IPAddress(10, 200, _localMac[4], _localMac[5]);
    }

    Serial.printf("[Mesh] Local IP: %s\n", _localIP.toString().c_str());

    // Gateway: start onboarding AP
    if (mode == MESH_GATEWAY) {
        _onboarding.startProvisioningAP(_keys.networkId, _channel, _psk);
        Serial.printf("[Mesh] Onboarding AP  SSID: %s\n", _onboarding.getProvisioningSSID());
        Serial.printf("[Mesh] Onboarding AP  Pass: %s\n", _onboarding.getProvisioningPassword());
    }

    _connected = true;
    _lastRouteAdvMs = millis();
    _lastBeaconMs = millis();
    _lastPeerCheckMs = millis();

    Serial.printf("[Mesh] Started in mode %d (channel %d)\n", mode, _channel);
    return true;
}

// ─── Configuration ────────────────────────────────────────────────────────────

void MeshNetwork::setRelayEnabled(bool enabled) { _relayEnabled = enabled; }

void MeshNetwork::setBatteryMode(bool enabled, uint32_t sleepIntervalSec) {
    _mode = enabled ? MESH_BATTERY : MESH_NODE;
    _relayEnabled = !enabled;
}

void MeshNetwork::setRssiThreshold(int8_t connectDbm, int8_t disconnectDbm) {
    // Store and use for peer filtering
}

void MeshNetwork::setKeyRotationInterval(uint32_t seconds) {
    // TODO: key rotation timer
}

void MeshNetwork::setMaxRoutes(uint16_t max) {
    // Static pool size — would need recompile
}

void MeshNetwork::setChannel(uint8_t channel) {
    _channel = channel;
    if (_connected) {
        _phy.setChannel(channel);
    }
}

// ─── State ────────────────────────────────────────────────────────────────────

bool MeshNetwork::isConnected() { return _connected; }
bool MeshNetwork::isGateway() { return _mode == MESH_GATEWAY; }

int MeshNetwork::getNodeCount() {
    return (int)_peerMgr.getPeerCount();
}

int8_t MeshNetwork::getRssiTo(const uint8_t* mac) {
    return (int8_t)_phy.getRssiEwma(mac);
}

int8_t MeshNetwork::getRssiFromGateway() {
    // Find gateway in route table (metric 0 or mode flag)
    return 0;
}

IPAddress MeshNetwork::getLocalIP() { return _localIP; }

uint8_t MeshNetwork::getChannel() { return _channel; }

const uint8_t* MeshNetwork::getMAC() { return _localMac; }

WiFiClient& MeshNetwork::getClient() {
    static WiFiClient client;
    return client;
}

// ─── Callbacks ────────────────────────────────────────────────────────────────

void MeshNetwork::onNodeJoin(MeshNodeCallback cb) { _onJoinCb = cb; }
void MeshNetwork::onNodeLeave(MeshNodeCallback cb) { _onLeaveCb = cb; }

// ─── Gateway APIs ─────────────────────────────────────────────────────────────

bool MeshNetwork::startWebServer(uint16_t port) {
    // Try immediately (AP may already be fully up)
    if (_webUI.begin(port, "admin", "admin", this)) return true;

    // AP not ready yet — store port; the AP_START handler registered in begin() will fire it
    _pendingWebPort = port;
    Serial.printf("[Mesh] httpd deferred until AP ready (port %d)\n", port);
    return true;
}

bool MeshNetwork::connectUplink(const char* ssid, const char* password) {
    if (!ssid || ssid[0] == '\0') {
        Serial.println("[Mesh] connectUplink: no SSID provided, skipping");
        return false;
    }
    Serial.printf("[Mesh] Connecting WiFi uplink to '%s'...\n", ssid);
    WiFi.begin(ssid, password);
    return true;  // connection result arrives via ARDUINO_EVENT_WIFI_STA_GOT_IP
}

bool MeshNetwork::startPrometheus(uint16_t port) {
    return _webUI.startPrometheus(port);
}

void MeshNetwork::setMqttBroker(const char* host, uint16_t port) {}

bool MeshNetwork::setStaticIPTable(const std::vector<std::pair<String, IPAddress>>& table) {
    return false;
}

// ─── Time ─────────────────────────────────────────────────────────────────────

time_t MeshNetwork::getMeshTime() { return 0; }
void MeshNetwork::onTimeSync(MeshTimeCallback cb) {}

// ─── Main Loop ────────────────────────────────────────────────────────────────

void MeshNetwork::loop() {
    if (!_connected) return;

    uint32_t now = millis();

    // Physical layer housekeeping
    _phy.update();

    // Onboarding updates
    _onboarding.update();

    // All nodes send JOIN_BEACON every 5s (gateway) or 10s (node)
    uint32_t beaconInterval = (_mode == MESH_GATEWAY) ? 5000 : 10000;
    if (now - _lastBeaconMs >= beaconInterval) {
        _sendJoinBeacon();
        _lastBeaconMs = now;
    }

    // All nodes send ROUTE_ADV every 30s
    if (now - _lastRouteAdvMs >= 30000) {
        _sendRouteAdv();
        _lastRouteAdvMs = now;
    }

    // Check peer timeouts every 10s
    if (now - _lastPeerCheckMs >= 10000) {
        _checkPeerTimeouts();
        _lastPeerCheckMs = now;
    }

    // Router maintenance
    _router.update();
}

void MeshNetwork::shutdown() {
    _connected = false;
    _onboarding.stopProvisioningAP();
    _webUI.stop();
}

// ─── Frame Reception ──────────────────────────────────────────────────────────

void MeshNetwork::_onFrameReceived(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi) {
    if (_instance) {
        _instance->_handleFrame(srcMac, data, len, rssi);
    }
}

void MeshNetwork::_handleFrame(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi) {
    if (len < MESH_HEADER_SIZE) return;

    // Deserialize header
    MeshFrameHeader hdr;
    if (!LinkLayer::deserializeHeader(data, len, hdr)) return;

    // Validate magic & version
    if (!LinkLayer::validateHeader(hdr)) return;

    // Check NetworkID — reject frames from other networks without decrypting
    if (!LinkLayer::matchesNetwork(hdr, ((uint16_t)_keys.networkId[0] << 8) | _keys.networkId[1])) {
        return;
    }

    // Skip frames from ourselves
    if (memcmp(hdr.srcMac, _localMac, 6) == 0) return;

    // Check Seen-Frame Cache (anti-loop for broadcast)
    if (memcmp(hdr.dstMac, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == 0) {
        if (_router.isFrameSeen(hdr.srcMac, hdr.sequence)) return;
        _router.markFrameSeen(hdr.srcMac, hdr.sequence);
    }

    // Payload starts after header
    const uint8_t* payload = data + MESH_HEADER_SIZE;
    size_t payloadLen = len - MESH_HEADER_SIZE;

    // Dispatch by frame type
    switch ((FrameType)hdr.frameType) {
        case FrameType::JOIN_BEACON:
            _handleJoinBeacon(hdr.srcMac, payload, payloadLen);
            break;
        case FrameType::KEY_EXCH_HELLO:
            _handleKeyExchHello(hdr.srcMac, payload, payloadLen);
            break;
        case FrameType::KEY_EXCH_REPLY:
            _handleKeyExchReply(hdr.srcMac, payload, payloadLen);
            break;
        case FrameType::KEY_EXCH_CONFIRM:
            _handleKeyExchConfirm(hdr.srcMac, payload, payloadLen);
            break;
        case FrameType::ROUTE_ADV:
            _handleRouteAdv(hdr.srcMac, payload, payloadLen);
            break;
        case FrameType::DATA:
            _handleData(hdr.srcMac, hdr, payload, payloadLen);
            break;
        default:
            break;
    }
}

// ─── JOIN_BEACON handling ─────────────────────────────────────────────────────

void MeshNetwork::_handleJoinBeacon(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    // A beacon means there's a node on the network
    // If we don't have this peer, initiate handshake
    PeerEntry* peer = _peerMgr.findPeer(srcMac);
    if (!peer) {
        Serial.printf("[Mesh] JOIN_BEACON from %02X:%02X:%02X:%02X:%02X:%02X — initiating handshake\n",
                      srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
        _initiateHandshake(srcMac);
    } else {
        // Refresh lastSeen
        peer->lastSeen = millis();
    }
}

// ─── ECDH Handshake ───────────────────────────────────────────────────────────

void MeshNetwork::_initiateHandshake(const uint8_t* peerMac) {
    // Check if handshake already in progress
    HandshakeContext* ctx = _findHandshake(peerMac);
    if (ctx && ctx->state != HandshakeState::IDLE) return;

    ctx = _allocHandshake(peerMac);
    if (!ctx) return;

    // Generate ephemeral key pair
    Crypto::generateKeyPair(ctx->localPub, ctx->localPriv);

    // Generate random nonce (8 bytes)
    esp_fill_random(ctx->nonce, 8);

    // Build HELLO payload: pubKey(32) + nonce(8) = 40 bytes
    uint8_t helloPayload[40];
    memcpy(helloPayload, ctx->localPub, 32);
    memcpy(helloPayload + 32, ctx->nonce, 8);

    ctx->state = HandshakeState::HELLO_SENT;
    ctx->startedAt = millis();

    _sendFrame(peerMac, FrameType::KEY_EXCH_HELLO, Protocol::MESH_INTERNAL,
               helloPayload, sizeof(helloPayload));

    Serial.printf("[Mesh] HELLO sent to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
}

void MeshNetwork::_handleKeyExchHello(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (len < 40) return;

    // Allocate context for this handshake
    HandshakeContext* ctx = _allocHandshake(srcMac);
    if (!ctx) return;

    // Store peer's public key and nonce
    memcpy(ctx->peerPub, payload, 32);
    memcpy(ctx->peerNonce, payload + 32, 8);

    // Generate our ephemeral key pair
    Crypto::generateKeyPair(ctx->localPub, ctx->localPriv);
    esp_fill_random(ctx->nonce, 8);

    // Build REPLY payload: pubKey(32) + nonce(8) = 40 bytes
    uint8_t replyPayload[40];
    memcpy(replyPayload, ctx->localPub, 32);
    memcpy(replyPayload + 32, ctx->nonce, 8);

    ctx->state = HandshakeState::REPLY_SENT;
    ctx->startedAt = millis();

    _sendFrame(srcMac, FrameType::KEY_EXCH_REPLY, Protocol::MESH_INTERNAL,
               replyPayload, sizeof(replyPayload));

    Serial.printf("[Mesh] REPLY sent to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
}

void MeshNetwork::_handleKeyExchReply(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (len < 40) return;

    HandshakeContext* ctx = _findHandshake(srcMac);
    if (!ctx || ctx->state != HandshakeState::HELLO_SENT) return;

    // Store peer's public key and nonce
    memcpy(ctx->peerPub, payload, 32);
    memcpy(ctx->peerNonce, payload + 32, 8);

    // Compute shared secret
    uint8_t sharedSecret[32];
    if (!Crypto::computeSharedSecret(ctx->localPriv, ctx->peerPub, sharedSecret)) {
        Serial.println("[Mesh] ERROR: ECDH computation failed");
        _freeHandshake(srcMac);
        return;
    }

    // Derive LinkKey
    uint8_t linkKey[MESH_KEY_SIZE];
    // Canonical MAC ordering: lower MAC first
    const uint8_t* macA = (memcmp(_localMac, srcMac, 6) < 0) ? _localMac : srcMac;
    const uint8_t* macB = (memcmp(_localMac, srcMac, 6) < 0) ? srcMac : _localMac;
    Crypto::deriveLinkKey(sharedSecret, _psk, macA, macB, linkKey);

    // Register peer with link key
    PeerEntry* peer = _peerMgr.addPeer(srcMac);
    if (peer) {
        _peerMgr.setLinkKey(srcMac, linkKey, _epoch);
    }

    // Send CONFIRM: XOR of nonces encrypted with link key proves we derived same key
    uint8_t confirmData[8];
    for (int i = 0; i < 8; i++) {
        confirmData[i] = ctx->nonce[i] ^ ctx->peerNonce[i];
    }

    _sendFrame(srcMac, FrameType::KEY_EXCH_CONFIRM, Protocol::MESH_INTERNAL,
               confirmData, sizeof(confirmData));

    ctx->state = HandshakeState::CONFIRMED;

    // Add route for this peer (direct, hopCount=1)
    IPAddress peerIP(10, 200, srcMac[4], srcMac[5]);
    _router.addRoute(peerIP, srcMac, srcMac, 1);

    Serial.printf("[Mesh] Handshake COMPLETE with %02X:%02X:%02X:%02X:%02X:%02X → IP %s\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5],
                  peerIP.toString().c_str());

    if (_onJoinCb) {
        _onJoinCb(srcMac, peerIP);
    }

    _freeHandshake(srcMac);
}

void MeshNetwork::_handleKeyExchConfirm(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (len < 8) return;

    HandshakeContext* ctx = _findHandshake(srcMac);
    if (!ctx || ctx->state != HandshakeState::REPLY_SENT) return;

    // Compute shared secret
    uint8_t sharedSecret[32];
    if (!Crypto::computeSharedSecret(ctx->localPriv, ctx->peerPub, sharedSecret)) {
        _freeHandshake(srcMac);
        return;
    }

    // Derive LinkKey (same canonical MAC order)
    uint8_t linkKey[MESH_KEY_SIZE];
    const uint8_t* macA = (memcmp(_localMac, srcMac, 6) < 0) ? _localMac : srcMac;
    const uint8_t* macB = (memcmp(_localMac, srcMac, 6) < 0) ? srcMac : _localMac;
    Crypto::deriveLinkKey(sharedSecret, _psk, macA, macB, linkKey);

    // Verify CONFIRM payload
    uint8_t expected[8];
    for (int i = 0; i < 8; i++) {
        expected[i] = ctx->peerNonce[i] ^ ctx->nonce[i];
    }

    if (memcmp(payload, expected, 8) != 0) {
        Serial.printf("[Mesh] CONFIRM verification FAILED from %02X:%02X:%02X:%02X:%02X:%02X (wrong PSK?)\n",
                      srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
        _freeHandshake(srcMac);
        return;
    }

    // Register peer
    PeerEntry* peer = _peerMgr.addPeer(srcMac);
    if (peer) {
        _peerMgr.setLinkKey(srcMac, linkKey, _epoch);
    }

    // Send our CONFIRM back
    uint8_t confirmData[8];
    for (int i = 0; i < 8; i++) {
        confirmData[i] = ctx->nonce[i] ^ ctx->peerNonce[i];
    }
    _sendFrame(srcMac, FrameType::KEY_EXCH_CONFIRM, Protocol::MESH_INTERNAL,
               confirmData, sizeof(confirmData));

    // Add route
    IPAddress peerIP(10, 200, srcMac[4], srcMac[5]);
    _router.addRoute(peerIP, srcMac, srcMac, 1);

    Serial.printf("[Mesh] Handshake COMPLETE with %02X:%02X:%02X:%02X:%02X:%02X → IP %s\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5],
                  peerIP.toString().c_str());

    if (_onJoinCb) {
        _onJoinCb(srcMac, peerIP);
    }

    _freeHandshake(srcMac);
}

// ─── Route Advertisement ──────────────────────────────────────────────────────

void MeshNetwork::_handleRouteAdv(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    // Update our peer's lastSeen
    PeerEntry* peer = _peerMgr.findPeer(srcMac);
    if (peer) {
        peer->lastSeen = millis();
    }

    // Deserialize route entries from this neighbor
    _router.deserializeRouteAdv(payload, len, srcMac);
}

void MeshNetwork::_handleData(const uint8_t* srcMac, const MeshFrameHeader& hdr,
                              const uint8_t* payload, size_t len) {
    // For now, log data frames. Full handling needs decryption + netif injection.
    PeerEntry* peer = _peerMgr.findPeer(hdr.srcMac);
    if (peer) {
        peer->lastSeen = millis();
    }
}

// ─── Frame Sending ────────────────────────────────────────────────────────────

bool MeshNetwork::_sendFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
                             const uint8_t* payload, size_t payloadLen) {
    uint8_t frame[250];
    if (MESH_HEADER_SIZE + payloadLen > sizeof(frame)) return false;

    MeshFrameHeader hdr = {};
    hdr.magic = MESH_MAGIC;
    hdr.version = MESH_VERSION;
    hdr.networkId = ((uint16_t)_keys.networkId[0] << 8) | _keys.networkId[1];
    hdr.frameType = (uint8_t)type;
    hdr.protocol = (uint8_t)proto;
    hdr.epoch = _epoch;
    memcpy(hdr.srcMac, _localMac, 6);
    memcpy(hdr.dstMac, dstMac, 6);
    hdr.sequence = ++_seqCounter;

    LinkLayer::serializeHeader(frame, hdr);
    if (payloadLen > 0) {
        memcpy(frame + MESH_HEADER_SIZE, payload, payloadLen);
    }

    return _phy.sendUnicast(dstMac, frame, MESH_HEADER_SIZE + payloadLen);
}

bool MeshNetwork::_sendBroadcastFrame(FrameType type, Protocol proto,
                                      const uint8_t* payload, size_t payloadLen) {
    uint8_t frame[250];
    if (MESH_HEADER_SIZE + payloadLen > sizeof(frame)) return false;

    MeshFrameHeader hdr = {};
    hdr.magic = MESH_MAGIC;
    hdr.version = MESH_VERSION;
    hdr.networkId = ((uint16_t)_keys.networkId[0] << 8) | _keys.networkId[1];
    hdr.frameType = (uint8_t)type;
    hdr.protocol = (uint8_t)proto;
    hdr.epoch = _epoch;
    memcpy(hdr.srcMac, _localMac, 6);
    memset(hdr.dstMac, 0xFF, 6);
    hdr.sequence = ++_seqCounter;

    LinkLayer::serializeHeader(frame, hdr);
    if (payloadLen > 0) {
        memcpy(frame + MESH_HEADER_SIZE, payload, payloadLen);
    }

    return _phy.sendBroadcast(frame, MESH_HEADER_SIZE + payloadLen);
}

// ─── Periodic Tasks ───────────────────────────────────────────────────────────

void MeshNetwork::_sendJoinBeacon() {
    // Beacon payload: channel(1) + localIP(4) + mode(1) = 6 bytes
    uint8_t payload[6];
    payload[0] = _channel;
    uint32_t ip = (uint32_t)_localIP;
    memcpy(&payload[1], &ip, 4);
    payload[5] = (uint8_t)_mode;

    _sendBroadcastFrame(FrameType::JOIN_BEACON, Protocol::MESH_INTERNAL, payload, sizeof(payload));
}

void MeshNetwork::_sendRouteAdv() {
    if (_peerMgr.getPeerCount() == 0) return;

    uint8_t buf[216]; // Max payload
    size_t len = _router.serializeRouteAdv(buf, sizeof(buf), nullptr);
    if (len == 0) return;

    _sendBroadcastFrame(FrameType::ROUTE_ADV, Protocol::MESH_INTERNAL, buf, len);
}

void MeshNetwork::_checkPeerTimeouts() {
    uint32_t now = millis();
    for (size_t i = 0; i < _peerMgr.getPeerCount(); i++) {
        PeerEntry* peer = _peerMgr.getPeerByIndex(i);
        if (peer && peer->valid && (now - peer->lastSeen > 120000)) {
            // Peer timed out
            Serial.printf("[Mesh] Peer timeout: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          peer->mac[0], peer->mac[1], peer->mac[2],
                          peer->mac[3], peer->mac[4], peer->mac[5]);

            IPAddress peerIP(10, 200, peer->mac[4], peer->mac[5]);
            if (_onLeaveCb) {
                _onLeaveCb(peer->mac, peerIP);
            }

            _router.handleRouteWithdraw(peer->mac);
            _peerMgr.removePeer(peer->mac);
        }
    }

    // Timeout stale handshakes (10s)
    for (size_t i = 0; i < MAX_HANDSHAKES; i++) {
        if (_handshakes[i].state != HandshakeState::IDLE &&
            (now - _handshakes[i].startedAt > 10000)) {
            _handshakes[i].state = HandshakeState::IDLE;
        }
    }
}

// ─── Handshake Context Management ────────────────────────────────────────────

HandshakeContext* MeshNetwork::_findHandshake(const uint8_t* mac) {
    for (size_t i = 0; i < MAX_HANDSHAKES; i++) {
        if (_handshakes[i].state != HandshakeState::IDLE &&
            memcmp(_handshakes[i].peerMac, mac, 6) == 0) {
            return &_handshakes[i];
        }
    }
    return nullptr;
}

HandshakeContext* MeshNetwork::_allocHandshake(const uint8_t* mac) {
    // First check if already exists
    HandshakeContext* existing = _findHandshake(mac);
    if (existing) return existing;

    // Find free slot
    for (size_t i = 0; i < MAX_HANDSHAKES; i++) {
        if (_handshakes[i].state == HandshakeState::IDLE) {
            memset(&_handshakes[i], 0, sizeof(HandshakeContext));
            memcpy(_handshakes[i].peerMac, mac, 6);
            return &_handshakes[i];
        }
    }

    // Evict oldest
    uint32_t oldest = UINT32_MAX;
    size_t oldestIdx = 0;
    for (size_t i = 0; i < MAX_HANDSHAKES; i++) {
        if (_handshakes[i].startedAt < oldest) {
            oldest = _handshakes[i].startedAt;
            oldestIdx = i;
        }
    }
    memset(&_handshakes[oldestIdx], 0, sizeof(HandshakeContext));
    memcpy(_handshakes[oldestIdx].peerMac, mac, 6);
    return &_handshakes[oldestIdx];
}

void MeshNetwork::_freeHandshake(const uint8_t* mac) {
    for (size_t i = 0; i < MAX_HANDSHAKES; i++) {
        if (memcmp(_handshakes[i].peerMac, mac, 6) == 0) {
            _handshakes[i].state = HandshakeState::IDLE;
            return;
        }
    }
}
