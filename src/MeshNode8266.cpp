#ifdef ESP8266

#include "MeshNode8266.h"
#include <EEPROM.h>
#include <cstring>

// ─── Singleton for static callback ──────────────────────────────────

MeshNode8266* _meshNode8266Instance = nullptr;

// ─── EEPROM layout for broker persistence ────────────────────────────

#define EEPROM_BROKER_OFFSET  0
#define EEPROM_BROKER_MAGIC   0xBE
#define EEPROM_SIZE           128

struct BrokerEepromData {
    uint8_t  magic;
    char     host[48];
    uint16_t port;
};

// ═══════════════════════════════════════════════════════════════════════
// Construction / Initialization
// ═══════════════════════════════════════════════════════════════════════

MeshNode8266::MeshNode8266() {
    _meshNode8266Instance = this;
}

bool MeshNode8266::begin(const char* psk, uint8_t channel) {
    _psk = psk;
    _channel = channel;

    // Derive network keys (NetworkKey + NetworkID)
    if (!Crypto::deriveNetworkKeys(_psk, _cryptoKeys)) {
        Serial.println("[Mesh8266] ERROR: key derivation failed");
        return false;
    }

    // Get local MAC
    WiFi.macAddress(_localMac);

    // Load broker from EEPROM if available
    EEPROM.begin(EEPROM_SIZE);
    BrokerEepromData stored;
    EEPROM.get(EEPROM_BROKER_OFFSET, stored);
    if (stored.magic == EEPROM_BROKER_MAGIC) {
        strncpy(_brokerHost, stored.host, sizeof(_brokerHost) - 1);
        _brokerPort = stored.port;
    }
    EEPROM.end();

    // Initialize physical layer
    _phy.onReceive(_rxCallback);

    // If channel specified, use it directly; otherwise scan
    if (_channel > 0) {
        if (!_phy.begin(_channel, _cryptoKeys.networkId)) {
            Serial.println("[Mesh8266] ERROR: physical layer init failed");
            return false;
        }
        _state = State::DISCOVERING;
        _startDiscovery();
    } else {
        _state = State::SCANNING;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// Main loop
// ═══════════════════════════════════════════════════════════════════════

void MeshNode8266::loop() {
    uint32_t now = millis();

    switch (_state) {
        case State::IDLE:
            break;

        case State::SCANNING:
            _scanForBeacon();
            break;

        case State::DISCOVERING:
            // Wait for offers until timeout, then select best
            if (now - _discoveryStartMs >= MESH8266_DISCOVERY_TIMEOUT_MS) {
                _selectBestProxy();
            }
            break;

        case State::HANDSHAKE:
            // Timeout handshake after 5 seconds
            if (now - _handshakeStartMs >= 5000) {
                Serial.println("[Mesh8266] Handshake timeout — retrying discovery");
                _state = State::DISCOVERING;
                _startDiscovery();
            }
            break;

        case State::CONNECTED:
            // Heartbeat / keepalive could go here
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// MQTT API
// ═══════════════════════════════════════════════════════════════════════

bool MeshNode8266::mqttPublish(const char* topic, const uint8_t* payload,
                               size_t len, uint8_t qos, bool retain) {
    if (_state != State::CONNECTED) return false;
    if (!topic || strlen(topic) == 0) return false;

    // Frame layout: ProxyMsgType(1) + qos(1) + retain(1) + topicLen(1) + topic + payload
    size_t topicLen = strlen(topic);
    size_t frameLen = 4 + topicLen + len;

    if (frameLen > MESH_MAX_PAYLOAD) return false;

    uint8_t buf[MESH_MAX_PAYLOAD];
    buf[0] = (uint8_t)ProxyMsgType::PROXY_PUBLISH;
    buf[1] = qos;
    buf[2] = retain ? 1 : 0;
    buf[3] = (uint8_t)topicLen;
    memcpy(buf + 4, topic, topicLen);
    memcpy(buf + 4 + topicLen, payload, len);

    return _sendEncryptedFrame(_proxyMac, FrameType::PROXY, Protocol::MESH_INTERNAL,
                               buf, frameLen);
}

bool MeshNode8266::mqttSubscribe(const char* topic, uint8_t qos) {
    if (!topic || strlen(topic) == 0) return false;

    // Store subscription locally
    int freeSlot = -1;
    for (int i = 0; i < MESH8266_MAX_SUBSCRIPTIONS; i++) {
        if (_subs[i].active && strcmp(_subs[i].topic, topic) == 0) {
            return true;  // Already subscribed
        }
        if (!_subs[i].active && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0) return false;  // No room

    strncpy(_subs[freeSlot].topic, topic, MESH8266_MAX_TOPIC_LEN - 1);
    _subs[freeSlot].topic[MESH8266_MAX_TOPIC_LEN - 1] = '\0';
    _subs[freeSlot].active = true;

    // Send to proxy if connected
    if (_state != State::CONNECTED) return true;  // Will resubscribe on connect

    size_t topicLen = strlen(topic);
    uint8_t buf[MESH_MAX_PAYLOAD];
    buf[0] = (uint8_t)ProxyMsgType::PROXY_SUBSCRIBE;
    buf[1] = qos;
    buf[2] = (uint8_t)topicLen;
    memcpy(buf + 3, topic, topicLen);

    return _sendEncryptedFrame(_proxyMac, FrameType::PROXY, Protocol::MESH_INTERNAL,
                               buf, 3 + topicLen);
}

bool MeshNode8266::mqttUnsubscribe(const char* topic) {
    if (!topic) return false;

    // Remove from local list
    for (int i = 0; i < MESH8266_MAX_SUBSCRIPTIONS; i++) {
        if (_subs[i].active && strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].active = false;
            break;
        }
    }

    if (_state != State::CONNECTED) return true;

    size_t topicLen = strlen(topic);
    uint8_t buf[MESH_MAX_PAYLOAD];
    buf[0] = (uint8_t)ProxyMsgType::PROXY_UNSUBSCRIBE;
    buf[1] = (uint8_t)topicLen;
    memcpy(buf + 2, topic, topicLen);

    return _sendEncryptedFrame(_proxyMac, FrameType::PROXY, Protocol::MESH_INTERNAL,
                               buf, 2 + topicLen);
}

void MeshNode8266::onMqttMessage(MqttMessageCallback cb) { _msgCb = cb; }
void MeshNode8266::onConnected(MeshConnectCallback cb) { _connCb = cb; }
bool MeshNode8266::isConnected() const { return _state == State::CONNECTED; }
IPAddress MeshNode8266::getProxyIP() const { return _proxyIP; }
int8_t MeshNode8266::getProxyRSSI() const { return _proxyRssi; }
void MeshNode8266::setSleepDuration(uint32_t seconds) { _sleepDurationSec = seconds; }

// ═══════════════════════════════════════════════════════════════════════
// Channel scanning
// ═══════════════════════════════════════════════════════════════════════

bool MeshNode8266::_scanForBeacon() {
    // Scan channels in order: 1, 6, 11, then 2-5, 7-10, 12-13
    static const uint8_t scanOrder[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
    static uint8_t scanIdx = 0;
    static uint32_t scanStartMs = 0;

    if (scanStartMs == 0) {
        // Start scanning on first channel
        scanStartMs = millis();
        _phy.begin(scanOrder[scanIdx], _cryptoKeys.networkId);
    }

    // Wait 200ms per channel
    if (millis() - scanStartMs >= 200) {
        scanIdx++;
        if (scanIdx >= sizeof(scanOrder)) {
            scanIdx = 0;  // Wrap around
        }
        _phy.setChannel(scanOrder[scanIdx]);
        scanStartMs = millis();
    }

    // If we received a beacon (_channel was set by _handleJoinBeacon), we're done
    if (_channel > 0 && _state != State::SCANNING) {
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════
// Proxy discovery
// ═══════════════════════════════════════════════════════════════════════

void MeshNode8266::_startDiscovery() {
    memset(_offers, 0, sizeof(_offers));
    _discoveryStartMs = millis();

    // Send PROXY_DISCOVERY broadcast (unencrypted — no handshake yet)
    uint8_t payload[1] = { (uint8_t)ProxyMsgType::PROXY_DISCOVERY };
    _sendFrame(nullptr, FrameType::PROXY, Protocol::MESH_INTERNAL, payload, 1);

    Serial.println("[Mesh8266] PROXY_DISCOVERY sent");
}

void MeshNode8266::_handleProxyOffer(const uint8_t* srcMac, const uint8_t* payload,
                                     size_t len, int8_t rssi) {
    // PROXY_OFFER payload: ProxyMsgType(1) + proxyIP(4)
    if (len < 5) return;
    if (payload[0] != (uint8_t)ProxyMsgType::PROXY_OFFER) return;

    // Store offer
    for (int i = 0; i < MESH8266_MAX_PROXY_OFFERS; i++) {
        if (!_offers[i].valid) {
            memcpy(_offers[i].mac, srcMac, 6);
            _offers[i].rssi = rssi;
            _offers[i].valid = true;

            Serial.printf("[Mesh8266] PROXY_OFFER from %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d\n",
                          srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5], rssi);
            break;
        }
    }
}

void MeshNode8266::_selectBestProxy() {
    int bestIdx = -1;
    int8_t bestRssi = -127;

    for (int i = 0; i < MESH8266_MAX_PROXY_OFFERS; i++) {
        if (_offers[i].valid && _offers[i].rssi > bestRssi) {
            bestRssi = _offers[i].rssi;
            bestIdx = i;
        }
    }

    if (bestIdx < 0) {
        // No offers received — retry discovery
        Serial.println("[Mesh8266] No proxy offers — retrying...");
        _startDiscovery();
        return;
    }

    // Select best proxy
    memcpy(_proxyMac, _offers[bestIdx].mac, 6);
    _proxyRssi = bestRssi;

    Serial.printf("[Mesh8266] Selected proxy %02X:%02X:%02X:%02X:%02X:%02X (RSSI=%d)\n",
                  _proxyMac[0], _proxyMac[1], _proxyMac[2], _proxyMac[3],
                  _proxyMac[4], _proxyMac[5], _proxyRssi);

    // Initiate ECDH handshake
    _initiateHandshake();
}

// ═══════════════════════════════════════════════════════════════════════
// ECDH Handshake
// ═══════════════════════════════════════════════════════════════════════

void MeshNode8266::_initiateHandshake() {
    _state = State::HANDSHAKE;
    _handshakeStartMs = millis();

    // Generate ephemeral key pair
    if (!Crypto::generateKeyPair(_localPub, _localPriv)) {
        Serial.println("[Mesh8266] ERROR: key generation failed");
        _state = State::DISCOVERING;
        _startDiscovery();
        return;
    }

    // Generate random nonce (8 bytes)
    for (int i = 0; i < 8; i += 4) {
        uint32_t r = *(volatile uint32_t*)0x3FF20E44;
        memcpy(_nonce + i, &r, (8 - i < 4) ? (8 - i) : 4);
    }

    // Build HELLO payload: pubKey(32) + nonce(8) = 40 bytes
    uint8_t helloPayload[40];
    memcpy(helloPayload, _localPub, 32);
    memcpy(helloPayload + 32, _nonce, 8);

    _sendFrame(_proxyMac, FrameType::KEY_EXCH_HELLO, Protocol::MESH_INTERNAL,
               helloPayload, sizeof(helloPayload));

    Serial.println("[Mesh8266] KEY_EXCH_HELLO sent");
}

void MeshNode8266::_handleKeyExchReply(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (_state != State::HANDSHAKE) return;
    if (memcmp(srcMac, _proxyMac, 6) != 0) return;
    if (len < 40) return;

    // Store peer's public key and nonce
    memcpy(_peerPub, payload, 32);
    memcpy(_peerNonce, payload + 32, 8);

    // Compute shared secret
    uint8_t sharedSecret[32];
    if (!Crypto::computeSharedSecret(_localPriv, _peerPub, sharedSecret)) {
        Serial.println("[Mesh8266] ERROR: ECDH computation failed");
        _state = State::DISCOVERING;
        _startDiscovery();
        return;
    }

    // Derive LinkKey
    const uint8_t* macA = (memcmp(_localMac, srcMac, 6) < 0) ? _localMac : srcMac;
    const uint8_t* macB = (memcmp(_localMac, srcMac, 6) < 0) ? srcMac : _localMac;
    Crypto::deriveLinkKey(sharedSecret, _psk, macA, macB, _linkKey);

    // Clear sensitive data
    memset(sharedSecret, 0, sizeof(sharedSecret));
    memset(_localPriv, 0, sizeof(_localPriv));

    // Send CONFIRM: XOR of nonces encrypted with link key
    uint8_t confirmData[8];
    for (int i = 0; i < 8; i++) {
        confirmData[i] = _nonce[i] ^ _peerNonce[i];
    }

    _sendEncryptedFrame(_proxyMac, FrameType::KEY_EXCH_CONFIRM, Protocol::MESH_INTERNAL,
                        confirmData, sizeof(confirmData));

    Serial.println("[Mesh8266] KEY_EXCH_CONFIRM sent — waiting for peer confirm");
}

void MeshNode8266::_handleKeyExchConfirm(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (_state != State::HANDSHAKE) return;
    if (memcmp(srcMac, _proxyMac, 6) != 0) return;

    // Verify confirm data: should be peerNonce XOR localNonce
    // (The proxy sends its perspective: peerNonce ^ localNonce)
    // Since we already have the link key established, the frame was decrypted successfully
    // → handshake complete

    _state = State::CONNECTED;

    Serial.printf("[Mesh8266] CONNECTED to proxy %02X:%02X:%02X:%02X:%02X:%02X\n",
                  _proxyMac[0], _proxyMac[1], _proxyMac[2], _proxyMac[3],
                  _proxyMac[4], _proxyMac[5]);

    // Send PROXY_CONNECT
    _sendProxyConnect();

    // Re-subscribe all stored subscriptions
    _resubscribeAll();

    // Notify user
    if (_connCb) _connCb();
}

void MeshNode8266::_sendProxyConnect() {
    uint8_t buf[1] = { (uint8_t)ProxyMsgType::PROXY_CONNECT };
    _sendEncryptedFrame(_proxyMac, FrameType::PROXY, Protocol::MESH_INTERNAL, buf, 1);
}

void MeshNode8266::_resubscribeAll() {
    for (int i = 0; i < MESH8266_MAX_SUBSCRIPTIONS; i++) {
        if (_subs[i].active) {
            size_t topicLen = strlen(_subs[i].topic);
            uint8_t buf[MESH_MAX_PAYLOAD];
            buf[0] = (uint8_t)ProxyMsgType::PROXY_SUBSCRIBE;
            buf[1] = 0;  // QoS 0
            buf[2] = (uint8_t)topicLen;
            memcpy(buf + 3, _subs[i].topic, topicLen);
            _sendEncryptedFrame(_proxyMac, FrameType::PROXY, Protocol::MESH_INTERNAL,
                                buf, 3 + topicLen);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// JOIN_BEACON handling (broker info extraction)
// ═══════════════════════════════════════════════════════════════════════

void MeshNode8266::_handleJoinBeacon(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (len < 6) return;

    uint8_t channel = payload[0];
    // payload[1..4] = IP, payload[5] = mode

    // If scanning, lock to this channel
    if (_state == State::SCANNING) {
        _channel = channel;
        _phy.setChannel(_channel);
        _state = State::DISCOVERING;
        _startDiscovery();
    }

    // Check for extended beacon with broker info (offset 10+)
    // Extended format: standard(6 or 10) + brokerLen(1) + broker(N) + port(2)
    size_t baseLen = (len >= 10) ? 10 : 6;  // battery nodes have 10-byte beacons
    if (len > baseLen + 3) {
        uint8_t brokerLen = payload[baseLen];
        if (brokerLen > 0 && baseLen + 1 + brokerLen + 2 <= len) {
            char newBroker[48] = {};
            size_t copyLen = (brokerLen < sizeof(newBroker) - 1) ? brokerLen : sizeof(newBroker) - 1;
            memcpy(newBroker, payload + baseLen + 1, copyLen);

            uint16_t newPort;
            memcpy(&newPort, payload + baseLen + 1 + brokerLen, 2);

            // Update if changed
            if (strcmp(newBroker, _brokerHost) != 0 || newPort != _brokerPort) {
                strncpy(_brokerHost, newBroker, sizeof(_brokerHost) - 1);
                _brokerPort = newPort;

                // Persist to EEPROM
                EEPROM.begin(EEPROM_SIZE);
                BrokerEepromData data;
                data.magic = EEPROM_BROKER_MAGIC;
                strncpy(data.host, _brokerHost, sizeof(data.host) - 1);
                data.port = _brokerPort;
                EEPROM.put(EEPROM_BROKER_OFFSET, data);
                EEPROM.commit();
                EEPROM.end();

                Serial.printf("[Mesh8266] Broker updated: %s:%u\n", _brokerHost, _brokerPort);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Incoming PROXY_MESSAGE handling
// ═══════════════════════════════════════════════════════════════════════

void MeshNode8266::_handleProxyMessage(const uint8_t* payload, size_t len) {
    // PROXY_MESSAGE: type(1) + topicLen(1) + topic(N) + payload(rest)
    if (len < 3) return;
    if (payload[0] != (uint8_t)ProxyMsgType::PROXY_MESSAGE) return;

    uint8_t topicLen = payload[1];
    if (2 + (size_t)topicLen > len) return;

    char topic[MESH8266_MAX_TOPIC_LEN] = {};
    size_t copyLen = (topicLen < sizeof(topic) - 1) ? topicLen : sizeof(topic) - 1;
    memcpy(topic, payload + 2, copyLen);

    const uint8_t* msgPayload = payload + 2 + topicLen;
    size_t msgLen = len - 2 - topicLen;

    if (_msgCb) {
        _msgCb(topic, msgPayload, msgLen);
    }
}

void MeshNode8266::_handleProxyPuback(const uint8_t* payload, size_t len) {
    // PROXY_PUBACK: type(1) + packetId(2)
    // For now just acknowledge — future: track pending publishes
    (void)payload;
    (void)len;
}

// ═══════════════════════════════════════════════════════════════════════
// Frame sending
// ═══════════════════════════════════════════════════════════════════════

bool MeshNode8266::_sendFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
                              const uint8_t* payload, size_t payloadLen) {
    // Build unencrypted frame (for discovery / pre-handshake)
    MeshFrameHeader hdr;
    hdr.magic = MESH_MAGIC;
    hdr.version = MESH_VERSION;
    hdr.networkId = ((uint16_t)_cryptoKeys.networkId[0] << 8) | _cryptoKeys.networkId[1];
    hdr.frameType = (uint8_t)type;
    hdr.protocol = (uint8_t)proto;
    hdr.epoch = _epoch;
    memcpy(hdr.srcMac, _localMac, 6);

    if (dstMac) {
        memcpy(hdr.dstMac, dstMac, 6);
    } else {
        memset(hdr.dstMac, 0xFF, 6);  // broadcast
    }
    hdr.sequence = ++_seqTx;

    uint8_t frame[MESH_FRAME_MAX_SIZE];
    size_t hdrLen = LinkLayer::serializeHeader(frame, hdr);
    memcpy(frame + hdrLen, payload, payloadLen);

    size_t totalLen = hdrLen + payloadLen;

    if (dstMac) {
        return _phy.sendUnicast(dstMac, frame, totalLen);
    } else {
        return _phy.sendBroadcast(frame, totalLen);
    }
}

bool MeshNode8266::_sendEncryptedFrame(const uint8_t* dstMac, FrameType type, Protocol proto,
                                       const uint8_t* payload, size_t payloadLen) {
    // Build header
    MeshFrameHeader hdr;
    hdr.magic = MESH_MAGIC;
    hdr.version = MESH_VERSION;
    hdr.networkId = ((uint16_t)_cryptoKeys.networkId[0] << 8) | _cryptoKeys.networkId[1];
    hdr.frameType = (uint8_t)type;
    hdr.protocol = (uint8_t)proto;
    hdr.epoch = _epoch;
    memcpy(hdr.srcMac, _localMac, 6);
    memcpy(hdr.dstMac, dstMac, 6);
    hdr.sequence = ++_seqTx;

    uint8_t frame[MESH_FRAME_MAX_SIZE];
    size_t hdrLen = LinkLayer::serializeHeader(frame, hdr);

    // Encrypt payload with GCM
    uint8_t nonce[MESH_GCM_NONCE_SIZE];
    Crypto::buildNonce(_epoch, hdr.sequence, _localMac, nonce);

    uint8_t ciphertext[MESH_MAX_PAYLOAD];
    uint8_t tag[MESH_GCM_TAG_SIZE];

    if (!Crypto::encrypt(_linkKey, nonce, frame, hdrLen,
                         payload, payloadLen, ciphertext, tag)) {
        return false;
    }

    memcpy(frame + hdrLen, ciphertext, payloadLen);
    memcpy(frame + hdrLen + payloadLen, tag, MESH_GCM_TAG_SIZE);

    size_t totalLen = hdrLen + payloadLen + MESH_GCM_TAG_SIZE;
    return _phy.sendUnicast(dstMac, frame, totalLen);
}

// ═══════════════════════════════════════════════════════════════════════
// Frame reception (static callback → instance dispatch)
// ═══════════════════════════════════════════════════════════════════════

void MeshNode8266::_rxCallback(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi) {
    if (_meshNode8266Instance) {
        _meshNode8266Instance->_onFrameReceived(srcMac, data, len, rssi);
    }
}

void MeshNode8266::_onFrameReceived(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi) {
    if (len < MESH_HEADER_SIZE) return;

    // Deserialize header
    MeshFrameHeader hdr;
    if (!LinkLayer::deserializeHeader(data, len, hdr)) return;

    // Verify magic and version
    if (hdr.magic != MESH_MAGIC || hdr.version != MESH_VERSION) return;

    // Verify NetworkID
    uint16_t myNetId = ((uint16_t)_cryptoKeys.networkId[0] << 8) | _cryptoKeys.networkId[1];
    if (hdr.networkId != myNetId) return;

    // Check destination (broadcast or us)
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (memcmp(hdr.dstMac, broadcast, 6) != 0 && memcmp(hdr.dstMac, _localMac, 6) != 0) return;

    const uint8_t* payload = data + MESH_HEADER_SIZE;
    size_t payloadLen = len - MESH_HEADER_SIZE;

    FrameType ftype = (FrameType)hdr.frameType;

    // Handle frame based on type and current state
    switch (ftype) {
        case FrameType::JOIN_BEACON:
            _handleJoinBeacon(srcMac, payload, payloadLen);
            break;

        case FrameType::PROXY:
            // PROXY frames before handshake (PROXY_OFFER) are unencrypted
            if (_state == State::DISCOVERING) {
                _handleProxyOffer(srcMac, payload, payloadLen, rssi);
            } else if (_state == State::CONNECTED) {
                // Decrypt first
                if (payloadLen < MESH_GCM_TAG_SIZE) break;
                size_t ctLen = payloadLen - MESH_GCM_TAG_SIZE;
                const uint8_t* tag = payload + ctLen;

                uint8_t nonce[MESH_GCM_NONCE_SIZE];
                Crypto::buildNonce(hdr.epoch, hdr.sequence, hdr.srcMac, nonce);

                uint8_t plaintext[MESH_MAX_PAYLOAD];
                if (!Crypto::decrypt(_linkKey, nonce, data, MESH_HEADER_SIZE,
                                     payload, ctLen, tag, plaintext)) {
                    break;  // Decryption failed
                }

                // Dispatch proxy sub-type
                if (ctLen > 0) {
                    ProxyMsgType ptype = (ProxyMsgType)plaintext[0];
                    switch (ptype) {
                        case ProxyMsgType::PROXY_MESSAGE:
                            _handleProxyMessage(plaintext, ctLen);
                            break;
                        case ProxyMsgType::PROXY_PUBACK:
                            _handleProxyPuback(plaintext, ctLen);
                            break;
                        default:
                            break;
                    }
                }
            }
            break;

        case FrameType::KEY_EXCH_REPLY:
            _handleKeyExchReply(srcMac, payload, payloadLen);
            break;

        case FrameType::KEY_EXCH_CONFIRM:
            if (_state == State::HANDSHAKE && payloadLen >= MESH_GCM_TAG_SIZE) {
                // Decrypt the confirm frame
                size_t ctLen = payloadLen - MESH_GCM_TAG_SIZE;
                const uint8_t* tag = payload + ctLen;

                uint8_t nonce[MESH_GCM_NONCE_SIZE];
                Crypto::buildNonce(hdr.epoch, hdr.sequence, hdr.srcMac, nonce);

                uint8_t plaintext[MESH_MAX_PAYLOAD];
                if (Crypto::decrypt(_linkKey, nonce, data, MESH_HEADER_SIZE,
                                    payload, ctLen, tag, plaintext)) {
                    _handleKeyExchConfirm(srcMac, plaintext, ctLen);
                }
            }
            break;

        default:
            break;
    }
}

#endif // ESP8266
