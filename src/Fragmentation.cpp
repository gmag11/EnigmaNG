#include "Fragmentation.h"
#include <cstring>

Fragmentation::Fragmentation() {
    memset(_sessions, 0, sizeof(_sessions));
}

uint8_t Fragmentation::fragment(const uint8_t* payload, size_t payloadLen,
                                uint16_t fragId, Fragment* outFragments, size_t maxFragments) {
    if (payloadLen == 0) return 0;

    uint8_t totalFrags = (uint8_t)((payloadLen + FRAG_MAX_PAYLOAD - 1) / FRAG_MAX_PAYLOAD);
    if (totalFrags > FRAG_MAX_FRAGMENTS || totalFrags > maxFragments) return 0;

    for (uint8_t i = 0; i < totalFrags; i++) {
        size_t offset = (size_t)i * FRAG_MAX_PAYLOAD;
        size_t chunkLen = payloadLen - offset;
        if (chunkLen > FRAG_MAX_PAYLOAD) chunkLen = FRAG_MAX_PAYLOAD;

        // Build fragment header
        MeshFragHeader fh = {};
        fh.fragId = fragId;
        fh.fragIndex = i;
        fh.fragTotal = totalFrags;

        LinkLayer::serializeFragHeader(outFragments[i].data, fh);
        memcpy(outFragments[i].data + MESH_FRAG_HEADER_SIZE, payload + offset, chunkLen);
        outFragments[i].length = MESH_FRAG_HEADER_SIZE + chunkLen;
    }

    return totalFrags;
}

const uint8_t* Fragmentation::reassemble(const uint8_t* srcMac, const uint8_t* fragPayload,
                                          size_t fragPayloadLen, size_t* outTotalLen) {
    if (fragPayloadLen < MESH_FRAG_HEADER_SIZE) return nullptr;

    MeshFragHeader fh;
    if (!LinkLayer::deserializeFragHeader(fragPayload, fragPayloadLen, fh)) return nullptr;
    if (fh.fragTotal == 0 || fh.fragTotal > FRAG_MAX_FRAGMENTS) return nullptr;
    if (fh.fragIndex >= fh.fragTotal) return nullptr;

    // Find or create session
    ReassemblySession* session = _findSession(srcMac, fh.fragId);
    if (!session) {
        session = _allocSession(srcMac, fh.fragId, fh.fragTotal);
        if (!session) return nullptr;
    }

    // Store fragment data (skip the 4-byte frag header)
    size_t dataLen = fragPayloadLen - MESH_FRAG_HEADER_SIZE;
    if (dataLen > FRAG_MAX_PAYLOAD) dataLen = FRAG_MAX_PAYLOAD;

    memcpy(session->buffer[fh.fragIndex], fragPayload + MESH_FRAG_HEADER_SIZE, dataLen);
    session->fragLengths[fh.fragIndex] = dataLen;
    session->receivedMask |= (1 << fh.fragIndex);

    // Check if all fragments received
    uint8_t expectedMask = (uint8_t)((1 << fh.fragTotal) - 1);
    if (session->receivedMask != expectedMask) return nullptr;

    // Reassemble into contiguous buffer
    size_t total = 0;
    for (uint8_t i = 0; i < fh.fragTotal; i++) {
        memcpy(_reassembledBuf + total, session->buffer[i], session->fragLengths[i]);
        total += session->fragLengths[i];
    }

    // Free session
    session->active = false;

    if (outTotalLen) *outTotalLen = total;
    return _reassembledBuf;
}

void Fragmentation::update() {
    uint32_t now = millis();
    for (size_t i = 0; i < FRAG_MAX_SESSIONS; i++) {
        if (_sessions[i].active && (now - _sessions[i].startedAt > FRAG_REASSEMBLY_TIMEOUT_MS)) {
            _sessions[i].active = false;
        }
    }
}

ReassemblySession* Fragmentation::_findSession(const uint8_t* srcMac, uint16_t fragId) {
    for (size_t i = 0; i < FRAG_MAX_SESSIONS; i++) {
        if (_sessions[i].active &&
            _sessions[i].fragId == fragId &&
            memcmp(_sessions[i].srcMac, srcMac, 6) == 0) {
            return &_sessions[i];
        }
    }
    return nullptr;
}

ReassemblySession* Fragmentation::_allocSession(const uint8_t* srcMac, uint16_t fragId, uint8_t totalFrags) {
    // Find free slot
    for (size_t i = 0; i < FRAG_MAX_SESSIONS; i++) {
        if (!_sessions[i].active) {
            memset(&_sessions[i], 0, sizeof(ReassemblySession));
            memcpy(_sessions[i].srcMac, srcMac, 6);
            _sessions[i].fragId = fragId;
            _sessions[i].totalFrags = totalFrags;
            _sessions[i].startedAt = millis();
            _sessions[i].active = true;
            return &_sessions[i];
        }
    }

    // Evict oldest session
    uint32_t oldest = UINT32_MAX;
    size_t oldestIdx = 0;
    for (size_t i = 0; i < FRAG_MAX_SESSIONS; i++) {
        if (_sessions[i].startedAt < oldest) {
            oldest = _sessions[i].startedAt;
            oldestIdx = i;
        }
    }
    memset(&_sessions[oldestIdx], 0, sizeof(ReassemblySession));
    memcpy(_sessions[oldestIdx].srcMac, srcMac, 6);
    _sessions[oldestIdx].fragId = fragId;
    _sessions[oldestIdx].totalFrags = totalFrags;
    _sessions[oldestIdx].startedAt = millis();
    _sessions[oldestIdx].active = true;
    return &_sessions[oldestIdx];
}
