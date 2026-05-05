#include "LinkLayer.h"

size_t LinkLayer::serializeHeader(uint8_t* buf, const MeshFrameHeader& header) {
    // Serialize in network byte order (big-endian) for multi-byte fields
    buf[0] = (uint8_t)(header.magic >> 8);
    buf[1] = (uint8_t)(header.magic & 0xFF);
    buf[2] = header.version;
    buf[3] = (uint8_t)(header.networkId >> 8);
    buf[4] = (uint8_t)(header.networkId & 0xFF);
    buf[5] = header.frameType;
    buf[6] = header.protocol;
    buf[7] = header.epoch;
    memcpy(&buf[8], header.srcMac, 6);
    memcpy(&buf[14], header.dstMac, 6);
    buf[20] = (uint8_t)(header.sequence >> 8);
    buf[21] = (uint8_t)(header.sequence & 0xFF);
    return MESH_HEADER_SIZE;
}

bool LinkLayer::deserializeHeader(const uint8_t* buf, size_t len, MeshFrameHeader& header) {
    if (len < MESH_HEADER_SIZE) return false;

    header.magic = ((uint16_t)buf[0] << 8) | buf[1];
    header.version = buf[2];
    header.networkId = ((uint16_t)buf[3] << 8) | buf[4];
    header.frameType = buf[5];
    header.protocol = buf[6];
    header.epoch = buf[7];
    memcpy(header.srcMac, &buf[8], 6);
    memcpy(header.dstMac, &buf[14], 6);
    header.sequence = ((uint16_t)buf[20] << 8) | buf[21];

    return true;
}

bool LinkLayer::validateHeader(const MeshFrameHeader& header) {
    if (header.magic != MESH_MAGIC) return false;
    if (header.version != MESH_VERSION) return false;
    if (header.frameType < (uint8_t)FrameType::DATA || header.frameType > (uint8_t)FrameType::PROXY) return false;
    return true;
}

bool LinkLayer::matchesNetwork(const MeshFrameHeader& header, uint16_t expectedNetworkId) {
    return header.networkId == expectedNetworkId;
}

size_t LinkLayer::serializeFragHeader(uint8_t* buf, const MeshFragHeader& fragHeader) {
    buf[0] = (uint8_t)(fragHeader.fragId >> 8);
    buf[1] = (uint8_t)(fragHeader.fragId & 0xFF);
    buf[2] = fragHeader.fragIndex;
    buf[3] = fragHeader.fragTotal;
    return MESH_FRAG_HEADER_SIZE;
}

bool LinkLayer::deserializeFragHeader(const uint8_t* buf, size_t len, MeshFragHeader& fragHeader) {
    if (len < MESH_FRAG_HEADER_SIZE) return false;

    fragHeader.fragId = ((uint16_t)buf[0] << 8) | buf[1];
    fragHeader.fragIndex = buf[2];
    fragHeader.fragTotal = buf[3];
    return true;
}
