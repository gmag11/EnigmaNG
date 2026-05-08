#if !defined(ESP8266)

#include "ServiceDiscovery.h"
#include <mdns.h>
#include <cstring>

bool ServiceDiscovery::registerService(const char* name, uint16_t port) {
    if (_localCount >= MAX_SERVICES) return false;

    MeshService& svc = _localServices[_localCount];
    strncpy(svc.name, name, SERVICE_NAME_MAX - 1);
    svc.port = port;
    svc.valid = true;
    _localCount++;
    return true;
}

bool ServiceDiscovery::unregisterService(const char* name) {
    for (size_t i = 0; i < _localCount; i++) {
        if (_localServices[i].valid && strncmp(_localServices[i].name, name, SERVICE_NAME_MAX) == 0) {
            _localServices[i].valid = false;
            return true;
        }
    }
    return false;
}

bool ServiceDiscovery::queryService(const char* name) {
    // Send SERVICE_QUERY broadcast frame
    // Actual frame construction delegated to LinkLayer
    // TODO: Integrate with MeshNetwork frame dispatch
    return true;
}

void ServiceDiscovery::handleServiceQuery(const uint8_t* fromMac, const char* name) {
    // Check if we have this service locally
    for (size_t i = 0; i < _localCount; i++) {
        if (_localServices[i].valid && strncmp(_localServices[i].name, name, SERVICE_NAME_MAX) == 0) {
            // Send SERVICE_REPLY to fromMac
            // TODO: Integrate with frame dispatch
            break;
        }
    }
}

void ServiceDiscovery::handleServiceReply(const uint8_t* fromMac, const char* name, IPAddress ip, uint16_t port) {
    // Update or add to discovered services
    for (size_t i = 0; i < _discoveredCount; i++) {
        if (_discoveredServices[i].valid && strncmp(_discoveredServices[i].name, name, SERVICE_NAME_MAX) == 0) {
            _discoveredServices[i].ip = ip;
            _discoveredServices[i].port = port;
            memcpy(_discoveredServices[i].nodeMac, fromMac, 6);
            _discoveredServices[i].lastSeen = millis();
            return;
        }
    }

    if (_discoveredCount < MAX_SERVICES) {
        MeshService& svc = _discoveredServices[_discoveredCount];
        strncpy(svc.name, name, SERVICE_NAME_MAX - 1);
        svc.ip = ip;
        svc.port = port;
        memcpy(svc.nodeMac, fromMac, 6);
        svc.lastSeen = millis();
        svc.valid = true;
        _discoveredCount++;
    }
}

const MeshService* ServiceDiscovery::findService(const char* name) {
    for (size_t i = 0; i < _discoveredCount; i++) {
        if (_discoveredServices[i].valid && strncmp(_discoveredServices[i].name, name, SERVICE_NAME_MAX) == 0) {
            return &_discoveredServices[i];
        }
    }
    return nullptr;
}

bool ServiceDiscovery::startMdnsBridge() {
    // Initialize mDNS
    if (mdns_init() != ESP_OK) return false;
    mdns_hostname_set("enigmang-gw");

    // Republish all discovered mesh services to mDNS
    for (size_t i = 0; i < _discoveredCount; i++) {
        if (_discoveredServices[i].valid) {
            // Parse service name to extract type and protocol
            // e.g., "_mqtt._tcp" -> service="_mqtt", proto="_tcp"
            mdns_service_add(NULL, _discoveredServices[i].name, "_tcp",
                           _discoveredServices[i].port, NULL, 0);
        }
    }
    return true;
}

size_t ServiceDiscovery::serializeServices(uint8_t* buf, size_t bufLen) {
    // Serialize local services for embedding in ROUTE_ADV
    // Format: count(1) + [name(16) + port(2)] per service
    size_t offset = 0;
    uint8_t count = 0;

    offset++; // Reserve byte for count

    for (size_t i = 0; i < _localCount && offset + 18 <= bufLen; i++) {
        if (!_localServices[i].valid) continue;
        memcpy(&buf[offset], _localServices[i].name, SERVICE_NAME_MAX);
        offset += SERVICE_NAME_MAX;
        buf[offset++] = (uint8_t)(_localServices[i].port >> 8);
        buf[offset++] = (uint8_t)(_localServices[i].port & 0xFF);
        count++;
    }

    buf[0] = count;
    return offset;
}

void ServiceDiscovery::deserializeServices(const uint8_t* buf, size_t len, const uint8_t* fromMac) {
    if (len < 1) return;
    uint8_t count = buf[0];
    size_t offset = 1;

    for (uint8_t i = 0; i < count && offset + 18 <= len; i++) {
        char name[SERVICE_NAME_MAX] = {};
        memcpy(name, &buf[offset], SERVICE_NAME_MAX);
        offset += SERVICE_NAME_MAX;
        uint16_t port = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
        offset += 2;

        handleServiceReply(fromMac, name, IPAddress(0, 0, 0, 0), port);
    }
}

void ServiceDiscovery::update() {
    // Expire old discovered services
    uint32_t now = millis();
    for (size_t i = 0; i < _discoveredCount; i++) {
        if (_discoveredServices[i].valid && (now - _discoveredServices[i].lastSeen > 120000)) {
            _discoveredServices[i].valid = false;
        }
    }
}

#endif // !ESP8266
