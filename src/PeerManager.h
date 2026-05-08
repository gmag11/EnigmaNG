#ifndef MESH_PEER_MANAGER_H
#define MESH_PEER_MANAGER_H

#if !defined(ESP8266)

#include <Arduino.h>
#include "Crypto.h"

#ifndef MESH_MAX_PEERS
#define MESH_MAX_PEERS 16
#endif

// Peer entry: 36 bytes (compact for RAM budget)
struct PeerEntry {
    uint8_t  mac[6];
    uint8_t  linkKey[MESH_KEY_SIZE];  // 16 bytes
    uint8_t  epoch;
    uint16_t lastSeqTx;
    uint16_t lastSeqRx;
    uint32_t lastSeen;        // millis timestamp
    float    rssiEwma;
    uint8_t  routeCount;      // Number of routes through this peer
    bool     valid;
    bool     keyEstablished;  // Handshake complete
    bool     isBattery;       // Peer declared MESH_BATTERY mode in JOIN_BEACON
    uint32_t sleepIntervalMs; // Declared sleep interval (ms); 0 = unknown
};

class PeerManager {
public:
    PeerManager();

    // Add or update a peer
    PeerEntry* addPeer(const uint8_t* mac);
    PeerEntry* findPeer(const uint8_t* mac);
    bool removePeer(const uint8_t* mac);

    // Get next sequence number for a peer (increments)
    uint16_t getNextSeqTx(const uint8_t* mac);

    // Anti-replay: check if seq is valid (newer than lastSeqRx)
    bool checkAndUpdateSeqRx(const uint8_t* mac, uint16_t seq);

    // LRU eviction: remove the peer with oldest lastSeen and routeCount==0
    bool evictLRU();

    // Iteration
    size_t getPeerCount() const;
    PeerEntry* getPeerByIndex(size_t index);

    // Set link key after successful handshake
    bool setLinkKey(const uint8_t* mac, const uint8_t* key, uint8_t epoch);

private:
    // Open-addressing hash table
    PeerEntry _peers[MESH_MAX_PEERS];
    size_t _count = 0;

    size_t _hash(const uint8_t* mac) const;
    int _probe(const uint8_t* mac) const;
};

#endif // !ESP8266
#endif // MESH_PEER_MANAGER_H
