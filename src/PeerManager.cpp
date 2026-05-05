#include "PeerManager.h"
#include <cstring>

PeerManager::PeerManager() {
    memset(_peers, 0, sizeof(_peers));
}

size_t PeerManager::_hash(const uint8_t* mac) const {
    // Simple hash from last 4 bytes of MAC
    uint32_t h = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                 ((uint32_t)mac[4] << 8) | mac[5];
    return h % MESH_MAX_PEERS;
}

int PeerManager::_probe(const uint8_t* mac) const {
    size_t idx = _hash(mac);
    for (size_t i = 0; i < MESH_MAX_PEERS; i++) {
        size_t pos = (idx + i) % MESH_MAX_PEERS;
        if (!_peers[pos].valid) return -1;
        if (memcmp(_peers[pos].mac, mac, 6) == 0) return (int)pos;
    }
    return -1;
}

PeerEntry* PeerManager::addPeer(const uint8_t* mac) {
    // Check if already exists
    int existing = _probe(mac);
    if (existing >= 0) {
        _peers[existing].lastSeen = millis();
        return &_peers[existing];
    }

    // Find empty slot using open addressing
    size_t idx = _hash(mac);
    for (size_t i = 0; i < MESH_MAX_PEERS; i++) {
        size_t pos = (idx + i) % MESH_MAX_PEERS;
        if (!_peers[pos].valid) {
            memset(&_peers[pos], 0, sizeof(PeerEntry));
            memcpy(_peers[pos].mac, mac, 6);
            _peers[pos].valid = true;
            _peers[pos].lastSeen = millis();
            _peers[pos].keyEstablished = false;
            _count++;
            return &_peers[pos];
        }
    }

    // Table full — try eviction
    if (evictLRU()) {
        return addPeer(mac); // Retry after eviction
    }

    return nullptr;
}

PeerEntry* PeerManager::findPeer(const uint8_t* mac) {
    int idx = _probe(mac);
    if (idx < 0) return nullptr;
    return &_peers[idx];
}

bool PeerManager::removePeer(const uint8_t* mac) {
    int idx = _probe(mac);
    if (idx < 0) return false;

    _peers[idx].valid = false;
    memset(&_peers[idx], 0, sizeof(PeerEntry));
    _count--;

    // Rehash subsequent entries (open addressing deletion)
    size_t pos = ((size_t)idx + 1) % MESH_MAX_PEERS;
    while (_peers[pos].valid) {
        PeerEntry tmp = _peers[pos];
        _peers[pos].valid = false;
        _count--;
        // Re-insert
        size_t newIdx = _hash(tmp.mac);
        for (size_t i = 0; i < MESH_MAX_PEERS; i++) {
            size_t newPos = (newIdx + i) % MESH_MAX_PEERS;
            if (!_peers[newPos].valid) {
                _peers[newPos] = tmp;
                _count++;
                break;
            }
        }
        pos = (pos + 1) % MESH_MAX_PEERS;
    }

    return true;
}

uint16_t PeerManager::getNextSeqTx(const uint8_t* mac) {
    PeerEntry* peer = findPeer(mac);
    if (!peer) return 0;
    return ++peer->lastSeqTx;
}

bool PeerManager::checkAndUpdateSeqRx(const uint8_t* mac, uint16_t seq) {
    PeerEntry* peer = findPeer(mac);
    if (!peer) return false;

    // Anti-replay: reject if seq <= lastSeqRx (with wrap-around handling)
    int16_t diff = (int16_t)(seq - peer->lastSeqRx);
    if (diff <= 0) return false;  // Replay or old frame

    peer->lastSeqRx = seq;
    return true;
}

bool PeerManager::evictLRU() {
    int candidate = -1;
    uint32_t oldestTime = UINT32_MAX;

    for (size_t i = 0; i < MESH_MAX_PEERS; i++) {
        if (_peers[i].valid && _peers[i].routeCount == 0) {
            if (_peers[i].lastSeen < oldestTime) {
                oldestTime = _peers[i].lastSeen;
                candidate = i;
            }
        }
    }

    if (candidate < 0) {
        // All peers have routes — evict oldest regardless
        for (size_t i = 0; i < MESH_MAX_PEERS; i++) {
            if (_peers[i].valid && _peers[i].lastSeen < oldestTime) {
                oldestTime = _peers[i].lastSeen;
                candidate = i;
            }
        }
    }

    if (candidate < 0) return false;

    return removePeer(_peers[candidate].mac);
}

size_t PeerManager::getPeerCount() const {
    return _count;
}

PeerEntry* PeerManager::getPeerByIndex(size_t index) {
    size_t found = 0;
    for (size_t i = 0; i < MESH_MAX_PEERS; i++) {
        if (_peers[i].valid) {
            if (found == index) return &_peers[i];
            found++;
        }
    }
    return nullptr;
}

bool PeerManager::setLinkKey(const uint8_t* mac, const uint8_t* key, uint8_t epoch) {
    PeerEntry* peer = findPeer(mac);
    if (!peer) return false;

    memcpy(peer->linkKey, key, MESH_KEY_SIZE);
    peer->epoch = epoch;
    peer->keyEstablished = true;
    peer->lastSeqTx = 0;
    peer->lastSeqRx = 0;
    return true;
}
