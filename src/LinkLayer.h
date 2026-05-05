#ifndef MESH_LINK_LAYER_H
#define MESH_LINK_LAYER_H

#include <Arduino.h>
#include <cstring>

// Frame header: 22 bytes
// Magic(2) + Version(1) + NetworkID(2) + FrameType(1) + Protocol(1) + Epoch(1) + SrcMAC(6) + DstMAC(6) + Seq(2)
#define MESH_HEADER_SIZE       22
#define MESH_GCM_TAG_SIZE      12
#define MESH_OVERHEAD          (MESH_HEADER_SIZE + MESH_GCM_TAG_SIZE)  // 34 bytes
#define MESH_MAX_PAYLOAD       216  // MTU
#define MESH_FRAME_MAX_SIZE    250  // ESP-NOW limit
#define MESH_MAGIC             0x454E  // "EN"
#define MESH_VERSION           0x01

// Fragment header: 4 extra bytes
#define MESH_FRAG_HEADER_SIZE  4

enum class FrameType : uint8_t {
    DATA             = 0x01,
    DATA_FRAG        = 0x02,
    KEY_EXCH_HELLO   = 0x03,
    KEY_EXCH_REPLY   = 0x04,
    KEY_EXCH_CONFIRM = 0x05,
    KEY_NACK         = 0x06,
    ROUTE_ADV        = 0x07,
    ROUTE_WITHDRAW   = 0x08,
    CONTROL          = 0x09,
    JOIN_BEACON      = 0x0A,
    ARP_QUERY        = 0x0B,
    ARP_REPLY        = 0x0C,
    SERVICE_QUERY    = 0x0D,
    SERVICE_REPLY    = 0x0E,
    PROXY            = 0x0F
};

enum class Protocol : uint8_t {
    MESH_INTERNAL    = 0x00,
    IPv4             = 0x01,
    IPv6_RESERVED    = 0x02,
    // 0x03-0xFF: User-defined
};

// Frame header structure
struct __attribute__((packed)) MeshFrameHeader {
    uint16_t magic;       // 0x454E "EN"
    uint8_t  version;     // 0x01
    uint16_t networkId;   // HKDF(PSK, "netid", 2)
    uint8_t  frameType;   // FrameType enum
    uint8_t  protocol;    // Protocol enum
    uint8_t  epoch;       // Current key epoch
    uint8_t  srcMac[6];   // End-to-end source
    uint8_t  dstMac[6];   // End-to-end destination (FF×6 = broadcast)
    uint16_t sequence;    // Anti-replay + nonce component
};

static_assert(sizeof(MeshFrameHeader) == MESH_HEADER_SIZE, "Header must be 22 bytes");

// Fragment header (appended after main header for DATA_FRAG)
struct __attribute__((packed)) MeshFragHeader {
    uint16_t fragId;      // Fragment group ID
    uint8_t  fragIndex;   // Fragment index (0-based)
    uint8_t  fragTotal;   // Total fragments
};

static_assert(sizeof(MeshFragHeader) == MESH_FRAG_HEADER_SIZE, "Fragment header must be 4 bytes");

class LinkLayer {
public:
    // Serialize header to buffer (must be at least MESH_HEADER_SIZE bytes)
    static size_t serializeHeader(uint8_t* buf, const MeshFrameHeader& header);

    // Deserialize header from buffer
    static bool deserializeHeader(const uint8_t* buf, size_t len, MeshFrameHeader& header);

    // Validate magic and version
    static bool validateHeader(const MeshFrameHeader& header);

    // Check NetworkID match (fast reject without decryption)
    static bool matchesNetwork(const MeshFrameHeader& header, uint16_t expectedNetworkId);

    // Fragment header serialization
    static size_t serializeFragHeader(uint8_t* buf, const MeshFragHeader& fragHeader);
    static bool deserializeFragHeader(const uint8_t* buf, size_t len, MeshFragHeader& fragHeader);
};

#endif // MESH_LINK_LAYER_H
