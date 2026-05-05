#include "Router.h"
#include <cstring>

Router::Router() {
    memset(_routes, 0, sizeof(_routes));
    memset(_seenCache, 0, sizeof(_seenCache));
}

RouteEntry* Router::addRoute(IPAddress destIP, const uint8_t* destMac,
                             const uint8_t* nextHopMac, uint8_t hopCount) {
    RouteEntry* existing = findRouteByIP(destIP);
    if (existing) {
        // Never overwrite a direct route (hopCount==1) with a worse one
        if (existing->hopCount == 1 && memcmp(existing->nextHopMac, nextHopMac, 6) != 0) {
            return existing;
        }
        // Update only if better, or same next-hop (refresh)
        if (hopCount < existing->hopCount || memcmp(existing->nextHopMac, nextHopMac, 6) == 0) {
            memcpy(existing->nextHopMac, nextHopMac, 6);
            existing->hopCount = hopCount;
            existing->metric = hopCount;
            existing->lastUpdated = millis();
            _topologyChanged = true;
        }
        return existing;
    }

    // Find empty slot
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (!_routes[i].valid) {
            _routes[i].destIP = destIP;
            memcpy(_routes[i].destMac, destMac, 6);
            memcpy(_routes[i].nextHopMac, nextHopMac, 6);
            _routes[i].hopCount = hopCount;
            _routes[i].metric = hopCount;
            _routes[i].lastUpdated = millis();
            _routes[i].valid = true;
            _routeCount++;
            _topologyChanged = true;
            return &_routes[i];
        }
    }

    // Table full — try eviction
    int candidate = _findEvictionCandidate();
    if (candidate >= 0) {
        _routes[candidate].destIP = destIP;
        memcpy(_routes[candidate].destMac, destMac, 6);
        memcpy(_routes[candidate].nextHopMac, nextHopMac, 6);
        _routes[candidate].hopCount = hopCount;
        _routes[candidate].metric = hopCount;
        _routes[candidate].lastUpdated = millis();
        _routes[candidate].valid = true;
        _topologyChanged = true;
        return &_routes[candidate];
    }

    return nullptr;
}

RouteEntry* Router::findRouteByIP(IPAddress destIP) {
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (_routes[i].valid && _routes[i].destIP == destIP) {
            return &_routes[i];
        }
    }
    return nullptr;
}

RouteEntry* Router::findRouteByMac(const uint8_t* destMac) {
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (_routes[i].valid && memcmp(_routes[i].destMac, destMac, 6) == 0) {
            return &_routes[i];
        }
    }
    return nullptr;
}

bool Router::removeRoute(IPAddress destIP) {
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (_routes[i].valid && _routes[i].destIP == destIP) {
            _routes[i].valid = false;
            _routeCount--;
            _topologyChanged = true;
            return true;
        }
    }
    return false;
}

void Router::expireRoutes() {
    uint32_t now = millis();
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (_routes[i].valid && (now - _routes[i].lastUpdated > ROUTE_EXPIRE_MS)) {
            _routes[i].valid = false;
            _routeCount--;
            _topologyChanged = true;
        }
    }
}

size_t Router::serializeRouteAdv(uint8_t* buf, size_t bufLen, const uint8_t* excludeNextHop) {
    // Each entry: destIP(4) + destMAC(6) + hopCount(1) + metric(1) = 12 bytes
    size_t offset = 0;
    size_t maxEntries = bufLen / ROUTE_ADV_ENTRY_SIZE;
    if (maxEntries > ROUTE_ADV_MAX_ENTRIES) maxEntries = ROUTE_ADV_MAX_ENTRIES;

    for (size_t i = 0; i < MESH_MAX_ROUTES && offset / ROUTE_ADV_ENTRY_SIZE < maxEntries; i++) {
        if (!_routes[i].valid) continue;

        // Split Horizon: don't advertise routes learned from this neighbor
        if (excludeNextHop && memcmp(_routes[i].nextHopMac, excludeNextHop, 6) == 0) {
            // Poison Reverse: advertise with hopCount=16 (infinity)
            uint32_t ip = (uint32_t)_routes[i].destIP;
            memcpy(&buf[offset], &ip, 4);
            memcpy(&buf[offset + 4], _routes[i].destMac, 6);
            buf[offset + 10] = 16; // Infinity (poison)
            buf[offset + 11] = 255;
            offset += ROUTE_ADV_ENTRY_SIZE;
            continue;
        }

        uint32_t ip = (uint32_t)_routes[i].destIP;
        memcpy(&buf[offset], &ip, 4);
        memcpy(&buf[offset + 4], _routes[i].destMac, 6);
        buf[offset + 10] = _routes[i].hopCount + 1; // Increment hop count
        buf[offset + 11] = _routes[i].metric;
        offset += ROUTE_ADV_ENTRY_SIZE;
    }

    return offset;
}

size_t Router::deserializeRouteAdv(const uint8_t* buf, size_t len,
                                   const uint8_t* fromMac,
                                   const uint8_t* localMac) {
    size_t count = 0;
    size_t entries = len / ROUTE_ADV_ENTRY_SIZE;

    for (size_t i = 0; i < entries; i++) {
        size_t offset = i * ROUTE_ADV_ENTRY_SIZE;

        uint32_t ipRaw;
        memcpy(&ipRaw, &buf[offset], 4);
        IPAddress destIP(ipRaw);

        uint8_t destMac[6];
        memcpy(destMac, &buf[offset + 4], 6);

        // Never install a route to ourselves
        if (localMac && memcmp(destMac, localMac, 6) == 0) continue;

        uint8_t hopCount = buf[offset + 10];

        if (hopCount >= 16) {
            RouteEntry* existing = findRouteByIP(destIP);
            if (existing && memcmp(existing->nextHopMac, fromMac, 6) == 0) {
                removeRoute(destIP);
            }
            continue;
        }

        addRoute(destIP, destMac, fromMac, hopCount);
        count++;
    }

    return count;
}

void Router::handleRouteWithdraw(const uint8_t* destMac) {
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (_routes[i].valid && memcmp(_routes[i].destMac, destMac, 6) == 0) {
            _routes[i].valid = false;
            _routeCount--;
            _topologyChanged = true;
        }
    }
}

bool Router::isFrameSeen(const uint8_t* srcMac, uint16_t seq) {
    uint32_t now = millis();
    for (size_t i = 0; i < MESH_SEEN_CACHE_SIZE; i++) {
        if (_seenCache[i].valid &&
            memcmp(_seenCache[i].srcMac, srcMac, 6) == 0 &&
            _seenCache[i].sequence == seq) {
            // Check TTL
            if (now - _seenCache[i].timestamp < SEEN_FRAME_TTL_MS) {
                return true;
            }
            _seenCache[i].valid = false;
        }
    }
    return false;
}

void Router::markFrameSeen(const uint8_t* srcMac, uint16_t seq) {
    // Circular buffer insertion
    _seenCache[_seenCacheIdx].valid = true;
    memcpy(_seenCache[_seenCacheIdx].srcMac, srcMac, 6);
    _seenCache[_seenCacheIdx].sequence = seq;
    _seenCache[_seenCacheIdx].timestamp = millis();
    _seenCacheIdx = (_seenCacheIdx + 1) % MESH_SEEN_CACHE_SIZE;
}

bool Router::shouldAdvertiseTo(const RouteEntry& route, const uint8_t* neighborMac) {
    // Split Horizon: don't advertise routes learned from this neighbor
    return memcmp(route.nextHopMac, neighborMac, 6) != 0;
}

bool Router::hasTopologyChanged() const {
    return _topologyChanged;
}

void Router::clearTopologyChanged() {
    _topologyChanged = false;
}

size_t Router::getRouteCount() const {
    return _routeCount;
}

RouteEntry* Router::getRouteByIndex(size_t index) {
    size_t found = 0;
    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (_routes[i].valid) {
            if (found == index) return &_routes[i];
            found++;
        }
    }
    return nullptr;
}

void Router::update() {
    expireRoutes();
    expireGateways();

    // Clean expired seen-frame entries
    uint32_t now = millis();
    for (size_t i = 0; i < MESH_SEEN_CACHE_SIZE; i++) {
        if (_seenCache[i].valid && (now - _seenCache[i].timestamp > SEEN_FRAME_TTL_MS)) {
            _seenCache[i].valid = false;
        }
    }
}

int Router::_findEvictionCandidate() {
    // Priority: expired > highest hopCount > oldest lastUpdated
    int candidate = -1;
    uint32_t worstScore = 0;
    uint32_t now = millis();

    for (size_t i = 0; i < MESH_MAX_ROUTES; i++) {
        if (!_routes[i].valid) continue;

        uint32_t age = now - _routes[i].lastUpdated;
        uint32_t score = ((uint32_t)_routes[i].hopCount << 24) | (age >> 8);

        if (score > worstScore) {
            worstScore = score;
            candidate = (int)i;
        }
    }

    return candidate;
}

// ─── Gateway Selection ────────────────────────────────────────────────────────

void Router::updateGateway(const uint8_t* gwMac, uint8_t hopCount, uint8_t uplinkMetric) {
    // Update existing entry
    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        if (_gateways[i].valid && memcmp(_gateways[i].mac, gwMac, 6) == 0) {
            _gateways[i].hopCount = hopCount;
            _gateways[i].uplinkMetric = uplinkMetric;
            _gateways[i].lastSeen = millis();
            return;
        }
    }

    // Find free slot
    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        if (!_gateways[i].valid) {
            memcpy(_gateways[i].mac, gwMac, 6);
            _gateways[i].hopCount = hopCount;
            _gateways[i].uplinkMetric = uplinkMetric;
            _gateways[i].lastSeen = millis();
            _gateways[i].valid = true;
            return;
        }
    }

    // Evict worst gateway (highest composite metric)
    size_t worstIdx = 0;
    uint16_t worstMetric = 0;
    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        uint16_t composite = (uint16_t)_gateways[i].hopCount * 50 + _gateways[i].uplinkMetric;
        if (composite > worstMetric) {
            worstMetric = composite;
            worstIdx = i;
        }
    }
    memcpy(_gateways[worstIdx].mac, gwMac, 6);
    _gateways[worstIdx].hopCount = hopCount;
    _gateways[worstIdx].uplinkMetric = uplinkMetric;
    _gateways[worstIdx].lastSeen = millis();
    _gateways[worstIdx].valid = true;
}

const GatewayEntry* Router::getBestGateway() const {
    const GatewayEntry* best = nullptr;
    uint16_t bestMetric = UINT16_MAX;

    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        if (!_gateways[i].valid) continue;
        // Composite: hopCount×50 + uplinkMetric (lower=better)
        uint16_t composite = (uint16_t)_gateways[i].hopCount * 50 + _gateways[i].uplinkMetric;
        if (composite < bestMetric) {
            bestMetric = composite;
            best = &_gateways[i];
        }
    }
    return best;
}

size_t Router::getGatewayCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        if (_gateways[i].valid) count++;
    }
    return count;
}

const GatewayEntry* Router::getGatewayByIndex(size_t index) const {
    size_t found = 0;
    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        if (_gateways[i].valid) {
            if (found == index) return &_gateways[i];
            found++;
        }
    }
    return nullptr;
}

void Router::expireGateways() {
    uint32_t now = millis();
    for (size_t i = 0; i < MESH_MAX_GATEWAYS; i++) {
        if (_gateways[i].valid && (now - _gateways[i].lastSeen > ROUTE_EXPIRE_MS * 2)) {
            _gateways[i].valid = false;
        }
    }
}
