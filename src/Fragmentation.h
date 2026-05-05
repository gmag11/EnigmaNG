#ifndef MESH_FRAGMENTATION_H
#define MESH_FRAGMENTATION_H

#include <Arduino.h>
#include "LinkLayer.h"

// Max payload per fragment: 250 - 22(header) - 4(frag header) = 224 bytes
// But with GCM tag overhead we use a conservative value
#define FRAG_MAX_PAYLOAD        (MESH_FRAME_MAX_SIZE - MESH_HEADER_SIZE - MESH_FRAG_HEADER_SIZE)
#define FRAG_REASSEMBLY_TIMEOUT_MS  2000
#define FRAG_MAX_FRAGMENTS      8    // Max fragments per message (8 × 224 = ~1.7KB max)
#define FRAG_MAX_SESSIONS       4    // Concurrent reassembly sessions

// Reassembly session
struct ReassemblySession {
    uint8_t  srcMac[6];
    uint16_t fragId;
    uint8_t  totalFrags;
    uint8_t  receivedMask;      // Bitmask of received fragments
    uint8_t  buffer[FRAG_MAX_FRAGMENTS][FRAG_MAX_PAYLOAD];
    size_t   fragLengths[FRAG_MAX_FRAGMENTS];
    uint32_t startedAt;
    bool     active;
};

class Fragmentation {
public:
    Fragmentation();

    // Fragment a large payload into multiple frames
    // Returns number of fragments created, or 0 on error
    // Caller must send each fragment via _sendFrame with FrameType::DATA_FRAG
    struct Fragment {
        uint8_t data[FRAG_MAX_PAYLOAD + MESH_FRAG_HEADER_SIZE];
        size_t  length;  // includes frag header
    };

    uint8_t fragment(const uint8_t* payload, size_t payloadLen,
                     uint16_t fragId, Fragment* outFragments, size_t maxFragments);

    // Process an incoming fragment. Returns:
    //   nullptr if reassembly is not yet complete
    //   pointer to reassembled data if complete (valid until next call or timeout)
    const uint8_t* reassemble(const uint8_t* srcMac, const uint8_t* fragPayload,
                              size_t fragPayloadLen, size_t* outTotalLen);

    // Expire stale sessions (call from loop)
    void update();

    // Get next fragment ID
    uint16_t nextFragId() { return ++_fragIdCounter; }

private:
    ReassemblySession _sessions[FRAG_MAX_SESSIONS];
    uint16_t _fragIdCounter = 0;
    uint8_t _reassembledBuf[FRAG_MAX_FRAGMENTS * FRAG_MAX_PAYLOAD];

    ReassemblySession* _findSession(const uint8_t* srcMac, uint16_t fragId);
    ReassemblySession* _allocSession(const uint8_t* srcMac, uint16_t fragId, uint8_t totalFrags);
};

#endif // MESH_FRAGMENTATION_H
