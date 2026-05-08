#if !defined(ESP8266)

#include "ProxyHandler.h"
#include "MeshNetwork.h"
#include "LinkLayer.h"
#include <cstring>

// ─── Static callback bridge ──────────────────────────────────────────

static ProxyHandler* s_proxyInstance = nullptr;

void ProxyHandler::_mqttCallback(char* topic, uint8_t* payload, unsigned int len) {
    if (s_proxyInstance) {
        s_proxyInstance->_onMqttMessage(topic, payload, len);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Construction / Initialization
// ═══════════════════════════════════════════════════════════════════════

ProxyHandler::ProxyHandler() : _mqtt(_tcpClient) {
    s_proxyInstance = this;
}

void ProxyHandler::setBroker(const char* host, uint16_t port) {
    strncpy(_brokerHost, host, sizeof(_brokerHost) - 1);
    _brokerPort = port;
}

void ProxyHandler::setBrokerAuth(const char* user, const char* password) {
    if (user) strncpy(_brokerUser, user, sizeof(_brokerUser) - 1);
    if (password) strncpy(_brokerPass, password, sizeof(_brokerPass) - 1);
}

void ProxyHandler::begin(MeshNetwork* mesh) {
    _mesh = mesh;
    _mqtt.setCallback(_mqttCallback);

    if (_brokerHost[0] != '\0') {
        _mqtt.setServer(_brokerHost, _brokerPort);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Main loop — maintain MQTT connection
// ═══════════════════════════════════════════════════════════════════════

void ProxyHandler::loop() {
    if (_brokerHost[0] == '\0') return;  // No broker configured

    _ensureMqttConnected();

    if (_mqtt.connected()) {
        _mqtt.loop();
    }
}

void ProxyHandler::_ensureMqttConnected() {
    if (_mqtt.connected()) return;

    uint32_t now = millis();
    if (now - _lastMqttReconnectMs < 5000) return;  // Rate-limit reconnections
    _lastMqttReconnectMs = now;

    Serial.printf("[Proxy] Connecting to MQTT %s:%u...\n", _brokerHost, _brokerPort);

    // Build client ID from MAC
    const uint8_t* mac = _mesh ? _mesh->getMAC() : nullptr;
    char clientId[32];
    if (mac) {
        snprintf(clientId, sizeof(clientId), "enigma-gw-%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
    } else {
        snprintf(clientId, sizeof(clientId), "enigma-gw-%lu", (unsigned long)millis());
    }

    bool ok;
    if (_brokerUser[0] != '\0') {
        ok = _mqtt.connect(clientId, _brokerUser, _brokerPass);
    } else {
        ok = _mqtt.connect(clientId);
    }

    if (ok) {
        Serial.println("[Proxy] MQTT connected");
        // Resubscribe all client topics
        for (int c = 0; c < PROXY_MAX_CLIENTS; c++) {
            if (!_clients[c].connected) continue;
            for (int s = 0; s < _clients[c].subCount; s++) {
                if (_clients[c].subscriptions[s][0] != '\0') {
                    _mqtt.subscribe(_clients[c].subscriptions[s]);
                }
            }
        }
    } else {
        Serial.printf("[Proxy] MQTT connect failed, rc=%d\n", _mqtt.state());
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Handle incoming PROXY frames
// ═══════════════════════════════════════════════════════════════════════

void ProxyHandler::handleDiscovery(const uint8_t* srcMac) {
    if (!_mesh) return;

    // Respond with PROXY_OFFER: type(1) + localIP(4)
    uint8_t offerPayload[5];
    offerPayload[0] = (uint8_t)ProxyMsgType::PROXY_OFFER;
    uint32_t ip = (uint32_t)_mesh->getLocalIP();
    memcpy(offerPayload + 1, &ip, 4);

    // Send unencrypted (no handshake with this node yet)
    _mesh->sendData(srcMac, offerPayload, sizeof(offerPayload));

    Serial.printf("[Proxy] PROXY_OFFER sent to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
}

void ProxyHandler::handleProxyFrame(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    if (len < 1) return;

    ProxyMsgType ptype = (ProxyMsgType)payload[0];

    switch (ptype) {
        case ProxyMsgType::PROXY_CONNECT:
            _handleConnect(srcMac);
            break;
        case ProxyMsgType::PROXY_DISCONNECT:
            _handleDisconnect(srcMac);
            break;
        case ProxyMsgType::PROXY_PUBLISH:
            _handlePublish(srcMac, payload, len);
            break;
        case ProxyMsgType::PROXY_SUBSCRIBE:
            _handleSubscribe(srcMac, payload, len);
            break;
        case ProxyMsgType::PROXY_UNSUBSCRIBE:
            _handleUnsubscribe(srcMac, payload, len);
            break;
        default:
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Client management
// ═══════════════════════════════════════════════════════════════════════

ProxyHandler::ProxyClient* ProxyHandler::_findClient(const uint8_t* mac) {
    for (int i = 0; i < PROXY_MAX_CLIENTS; i++) {
        if (_clients[i].connected && memcmp(_clients[i].mac, mac, 6) == 0) {
            return &_clients[i];
        }
    }
    return nullptr;
}

ProxyHandler::ProxyClient* ProxyHandler::_addClient(const uint8_t* mac) {
    // Check if already exists
    ProxyClient* existing = _findClient(mac);
    if (existing) return existing;

    // Find free slot
    for (int i = 0; i < PROXY_MAX_CLIENTS; i++) {
        if (!_clients[i].connected) {
            memcpy(_clients[i].mac, mac, 6);
            _clients[i].connected = true;
            _clients[i].subCount = 0;
            memset(_clients[i].subscriptions, 0, sizeof(_clients[i].subscriptions));
            return &_clients[i];
        }
    }
    return nullptr;  // Full
}

void ProxyHandler::_removeClient(const uint8_t* mac) {
    for (int i = 0; i < PROXY_MAX_CLIENTS; i++) {
        if (_clients[i].connected && memcmp(_clients[i].mac, mac, 6) == 0) {
            _clients[i].connected = false;
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Frame handlers
// ═══════════════════════════════════════════════════════════════════════

void ProxyHandler::_handleConnect(const uint8_t* srcMac) {
    ProxyClient* c = _addClient(srcMac);
    if (c) {
        Serial.printf("[Proxy] Client connected: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
    } else {
        Serial.println("[Proxy] ERROR: max clients reached");
    }
}

void ProxyHandler::_handleDisconnect(const uint8_t* srcMac) {
    // Unsubscribe all topics for this client
    ProxyClient* c = _findClient(srcMac);
    if (c) {
        for (int s = 0; s < c->subCount; s++) {
            if (c->subscriptions[s][0] != '\0' && _mqtt.connected()) {
                _mqtt.unsubscribe(c->subscriptions[s]);
            }
        }
    }
    _removeClient(srcMac);
    Serial.printf("[Proxy] Client disconnected: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
}

void ProxyHandler::_handlePublish(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    // Payload: type(1) + qos(1) + retain(1) + topicLen(1) + topic(N) + payload(rest)
    if (len < 5) return;

    uint8_t qos = payload[1];
    bool retain = payload[2] != 0;
    uint8_t topicLen = payload[3];

    if (4 + topicLen > len) return;

    char topic[64] = {};
    size_t copyLen = (topicLen < sizeof(topic) - 1) ? topicLen : sizeof(topic) - 1;
    memcpy(topic, payload + 4, copyLen);

    const uint8_t* msgPayload = payload + 4 + topicLen;
    size_t msgLen = len - 4 - topicLen;

    if (!_mqtt.connected()) {
        Serial.println("[Proxy] MQTT not connected — dropping publish");
        return;
    }

    bool ok = _mqtt.publish(topic, msgPayload, msgLen, retain);
    if (ok && qos > 0) {
        _sendProxyPuback(srcMac, 0);  // Simplified: packetId = 0
    }

    (void)qos;  // Full QoS tracking out of scope for v1.0
}

void ProxyHandler::_handleSubscribe(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    // Payload: type(1) + qos(1) + topicLen(1) + topic(N)
    if (len < 4) return;

    uint8_t topicLen = payload[2];
    if (3 + topicLen > len) return;

    char topic[64] = {};
    size_t copyLen = (topicLen < sizeof(topic) - 1) ? topicLen : sizeof(topic) - 1;
    memcpy(topic, payload + 3, copyLen);

    ProxyClient* c = _findClient(srcMac);
    if (!c) return;

    // Store subscription
    if (c->subCount < PROXY_MAX_SUBS_PER_CLIENT) {
        strncpy(c->subscriptions[c->subCount], topic, 63);
        c->subCount++;
    }

    // Subscribe on MQTT broker
    if (_mqtt.connected()) {
        _mqtt.subscribe(topic);
        Serial.printf("[Proxy] Subscribed '%s' for %02X:%02X\n", topic, srcMac[4], srcMac[5]);
    }
}

void ProxyHandler::_handleUnsubscribe(const uint8_t* srcMac, const uint8_t* payload, size_t len) {
    // Payload: type(1) + topicLen(1) + topic(N)
    if (len < 3) return;

    uint8_t topicLen = payload[1];
    if (2 + topicLen > len) return;

    char topic[64] = {};
    size_t copyLen = (topicLen < sizeof(topic) - 1) ? topicLen : sizeof(topic) - 1;
    memcpy(topic, payload + 2, copyLen);

    ProxyClient* c = _findClient(srcMac);
    if (!c) return;

    // Remove from subscription list
    for (int s = 0; s < c->subCount; s++) {
        if (strcmp(c->subscriptions[s], topic) == 0) {
            c->subscriptions[s][0] = '\0';
            break;
        }
    }

    if (_mqtt.connected()) {
        _mqtt.unsubscribe(topic);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// MQTT → ESP8266 message forwarding
// ═══════════════════════════════════════════════════════════════════════

void ProxyHandler::_onMqttMessage(char* topic, uint8_t* payload, unsigned int len) {
    // Find all clients subscribed to this topic and forward PROXY_MESSAGE
    for (int c = 0; c < PROXY_MAX_CLIENTS; c++) {
        if (!_clients[c].connected) continue;

        bool match = false;
        for (int s = 0; s < _clients[c].subCount; s++) {
            if (_clients[c].subscriptions[s][0] != '\0' &&
                strcmp(_clients[c].subscriptions[s], topic) == 0) {
                match = true;
                break;
            }
            // Simple wildcard: # at end matches any suffix
            size_t subLen = strlen(_clients[c].subscriptions[s]);
            if (subLen > 0 && _clients[c].subscriptions[s][subLen - 1] == '#') {
                if (strncmp(_clients[c].subscriptions[s], topic, subLen - 1) == 0) {
                    match = true;
                    break;
                }
            }
        }

        if (match) {
            _sendProxyMessage(_clients[c].mac, topic, payload, len);
        }
    }
}

void ProxyHandler::_sendProxyMessage(const uint8_t* dstMac, const char* topic,
                                     const uint8_t* payload, size_t len) {
    if (!_mesh) return;

    // Build PROXY_MESSAGE frame: type(1) + topicLen(1) + topic(N) + payload(M)
    size_t topicLen = strlen(topic);
    size_t frameLen = 2 + topicLen + len;
    if (frameLen > MESH_MAX_PAYLOAD) return;  // Too large

    uint8_t buf[MESH_MAX_PAYLOAD];
    buf[0] = (uint8_t)ProxyMsgType::PROXY_MESSAGE;
    buf[1] = (uint8_t)topicLen;
    memcpy(buf + 2, topic, topicLen);
    memcpy(buf + 2 + topicLen, payload, len);

    // Send encrypted via mesh (MeshNetwork handles encryption to peer)
    _mesh->sendData(dstMac, buf, frameLen);
}

void ProxyHandler::_sendProxyPuback(const uint8_t* dstMac, uint16_t packetId) {
    if (!_mesh) return;

    uint8_t buf[3];
    buf[0] = (uint8_t)ProxyMsgType::PROXY_PUBACK;
    buf[1] = (uint8_t)(packetId >> 8);
    buf[2] = (uint8_t)(packetId & 0xFF);

    _mesh->sendData(dstMac, buf, sizeof(buf));
}

#endif // !ESP8266
