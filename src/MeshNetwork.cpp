#include "MeshNetwork.h"
#include "PhysicalLayer.h"
#include "LinkLayer.h"
#include "Crypto.h"
#include "PeerManager.h"
#include "Router.h"

// Stub implementation — filled in by later phases

bool MeshNetwork::begin(const char* psk, MeshMode mode) {
    _mode = mode;
    _relayEnabled = (mode != MESH_BATTERY);
    // TODO: Initialize subsystems
    return false;
}

bool MeshNetwork::begin(const char* psk, IPAddress staticIP, MeshMode mode) {
    _mode = mode;
    _relayEnabled = (mode != MESH_BATTERY);
    // TODO: Initialize subsystems with static IP
    return false;
}

void MeshNetwork::setRelayEnabled(bool enabled) { _relayEnabled = enabled; }
void MeshNetwork::setBatteryMode(bool enabled, uint32_t sleepIntervalSec) {
    _mode = enabled ? MESH_BATTERY : MESH_NODE;
    _relayEnabled = !enabled;
}
void MeshNetwork::setRssiThreshold(int8_t connectDbm, int8_t disconnectDbm) {}
void MeshNetwork::setKeyRotationInterval(uint32_t seconds) {}
void MeshNetwork::setMaxRoutes(uint16_t max) {}

bool MeshNetwork::isConnected() { return _connected; }
bool MeshNetwork::isGateway() { return _mode == MESH_GATEWAY; }
int MeshNetwork::getNodeCount() { return 0; }
int8_t MeshNetwork::getRssiTo(const uint8_t* mac) { return 0; }
int8_t MeshNetwork::getRssiFromGateway() { return 0; }
IPAddress MeshNetwork::getLocalIP() { return IPAddress(0, 0, 0, 0); }

WiFiClient& MeshNetwork::getClient() {
    static WiFiClient client;
    return client;
}

void MeshNetwork::onNodeJoin(MeshNodeCallback cb) {}
void MeshNetwork::onNodeLeave(MeshNodeCallback cb) {}

bool MeshNetwork::startWebServer(uint16_t port) { return false; }
bool MeshNetwork::startPrometheus(uint16_t port) { return false; }
void MeshNetwork::setMqttBroker(const char* host, uint16_t port) {}
bool MeshNetwork::setStaticIPTable(const std::vector<std::pair<String, IPAddress>>& table) { return false; }

time_t MeshNetwork::getMeshTime() { return 0; }
void MeshNetwork::onTimeSync(MeshTimeCallback cb) {}

void MeshNetwork::loop() {
    // TODO: Process incoming frames, routing updates, etc.
}

void MeshNetwork::shutdown() {
    _connected = false;
}
