/**
 * Unit tests: encryption and decryption for each mesh frame type
 *
 * Verifies that:
 *  1. Frames that MUST be encrypted produce ciphertext different from plaintext.
 *  2. Ciphertext does NOT contain the original plaintext (no data leak).
 *  3. The receiver can decrypt and recover the exact original plaintext.
 *  4. Frames that are NOT encrypted contain the expected clear bytes.
 *  5. A wrong key causes AES-GCM authentication to fail.
 *  6. A tampered frame (flipped byte) fails GCM verification.
 *
 * Encryption rings per spec §4.2–§4.4:
 *   - NetworkKey : broadcast frames → ROUTE_ADV, ROUTE_WITHDRAW, ARP_QUERY,
 *                                        ARP_REPLY, SERVICE_QUERY, SERVICE_REPLY,
 *                                        KEY_NACK, CONTROL
 *   - LinkKey    : unicast frames   → DATA, DATA_FRAG, KEY_EXCH_CONFIRM, PROXY
 *   - Plaintext  : KEY_EXCH_HELLO, KEY_EXCH_REPLY, JOIN_BEACON
 *
 * NOTE: On PC mbedtls stubs operate with zeros, so cryptographic correctness
 * is validated on hardware. Here we check the flow (correct parameters,
 * frame structure, and GCM authentication properties).
 */

#include <unity.h>
#include <cstring>
#include <cstdlib>
#include "Crypto.h"
#include "LinkLayer.h"
#include "ServiceDiscovery.h"

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static const char*   PSK     = "TestNetworkPSK!1";
static const uint8_t MAC_A[] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t MAC_B[] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x66};
static const uint8_t MAC_BC[]= {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // broadcast

// Claves compartidas para todos los tests (derivadas en setUp global)
static uint8_t g_networkKey[MESH_KEY_SIZE];
static uint8_t g_networkId[MESH_NETWORK_ID_SIZE];
static uint8_t g_linkKeyA[MESH_KEY_SIZE];   // LinkKey NodeA→B (and B→A, symmetric)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Build the frame header and serialize it into @p headerBuf (22 bytes).
 */
static void buildHeader(uint8_t* headerBuf,
                        FrameType type,
                        Protocol  proto,
                        const uint8_t* srcMac,
                        const uint8_t* dstMac,
                        uint16_t  networkId,
                        uint8_t   epoch,
                        uint16_t  seq)
{
    MeshFrameHeader h = {};
    h.magic     = MESH_MAGIC;
    h.version   = MESH_VERSION;
    h.networkId = networkId;
    h.frameType = (uint8_t)type;
    h.protocol  = (uint8_t)proto;
    h.epoch     = epoch;
    memcpy(h.srcMac, srcMac, 6);
    memcpy(h.dstMac, dstMac, 6);
    h.sequence  = seq;
    LinkLayer::serializeHeader(headerBuf, h);
}

/**
 * Build a complete encrypted frame:
 *   frame = header(22) || ciphertext(ptLen) || tag(MESH_GCM_TAG_SIZE)
 *
 * @return  Total frame size, or 0 if encryption fails.
 */
static size_t buildEncryptedFrame(FrameType type, Protocol proto,
                                  const uint8_t* srcMac, const uint8_t* dstMac,
                                  uint16_t networkId, uint8_t epoch, uint16_t seq,
                                  const uint8_t* key,
                                  const uint8_t* plaintext, size_t ptLen,
                                  uint8_t* frame, size_t frameBufLen)
{
    if (frameBufLen < MESH_HEADER_SIZE + ptLen + MESH_GCM_TAG_SIZE) return 0;

    // 1. Header = AAD
    buildHeader(frame, type, proto, srcMac, dstMac, networkId, epoch, seq);

    // 2. Nonce from header fields
    uint8_t nonce[MESH_GCM_NONCE_SIZE];
    Crypto::buildNonce(epoch, seq, srcMac, nonce);

    // 3. Encrypt payload: ciphertext and tag go after the header
    uint8_t* ciphertext = frame + MESH_HEADER_SIZE;
    uint8_t* tag        = ciphertext + ptLen;

    bool ok = Crypto::encrypt(key, nonce,
                              frame, MESH_HEADER_SIZE,   // AAD = header
                              plaintext, ptLen,
                              ciphertext, tag);
    if (!ok) return 0;

    return MESH_HEADER_SIZE + ptLen + MESH_GCM_TAG_SIZE;
}

/**
 * Decrypt a frame produced by buildEncryptedFrame.
 *
 * @return  true if GCM ok and @p plaintextOut contains the original payload.
 */
static bool decryptFrame(const uint8_t* frame, size_t frameLen,
                         const uint8_t* key,
                         uint8_t* plaintextOut, size_t ptLen)
{
    if (frameLen < MESH_HEADER_SIZE + ptLen + MESH_GCM_TAG_SIZE) return false;

    MeshFrameHeader h = {};
    if (!LinkLayer::deserializeHeader(frame, frameLen, h)) return false;

    uint8_t nonce[MESH_GCM_NONCE_SIZE];
    Crypto::buildNonce(h.epoch, h.sequence, h.srcMac, nonce);

    const uint8_t* ciphertext = frame + MESH_HEADER_SIZE;
    const uint8_t* tag        = ciphertext + ptLen;

    return Crypto::decrypt(key, nonce,
                           frame, MESH_HEADER_SIZE,
                           ciphertext, ptLen,
                           tag, plaintextOut);
}

/**
 * Check that @p plaintext does not appear as a byte subsequence in @p buf.
 * Returns true if the plaintext is NOT present (correct: data encrypted).
 */
static bool noPlaintextLeak(const uint8_t* buf, size_t bufLen,
                            const uint8_t* plaintext, size_t ptLen)
{
    if (ptLen == 0) return true;
    for (size_t i = 0; i + ptLen <= bufLen; ++i) {
        if (memcmp(buf + i, plaintext, ptLen) == 0) return false;  // fuga detectada
    }
    return true;
}

// ---------------------------------------------------------------------------
// setUp / tearDown
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Key derivation (executed before all tests)
// ---------------------------------------------------------------------------

static void deriveTestKeys(void) {
    // Network Key + Network ID
    CryptoKeys nk = {};
    Crypto::deriveNetworkKeys(PSK, nk);
    memcpy(g_networkKey, nk.networkKey, MESH_KEY_SIZE);
    memcpy(g_networkId,  nk.networkId,  MESH_NETWORK_ID_SIZE);

    // LinkKey A↔B via simulated ECDH
    uint8_t pubA[MESH_ECDH_KEY_SIZE], privA[MESH_ECDH_KEY_SIZE];
    uint8_t pubB[MESH_ECDH_KEY_SIZE], privB[MESH_ECDH_KEY_SIZE];
    Crypto::generateKeyPair(pubA, privA);
    Crypto::generateKeyPair(pubB, privB);
    uint8_t secret[MESH_ECDH_KEY_SIZE];
    Crypto::computeSharedSecret(privA, pubB, secret);
    Crypto::deriveLinkKey(secret, PSK, MAC_A, MAC_B, g_linkKeyA);
}

// ---------------------------------------------------------------------------
// ── UNICAST FRAMES encrypted with LinkKey ─────────────────────────────────
// ---------------------------------------------------------------------------

// ── DATA ────────────────────────────────────────────────────────────────────
void test_data_frame_encrypted_and_decryptable(void) {
    const uint8_t payload[]  = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
                                 0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x11, 0x22, 0x33};
    const size_t  ptLen      = sizeof(payload);
    uint16_t      networkId  = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, /*epoch*/1, /*seq*/1,
        g_linkKeyA, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "DATA: buildEncryptedFrame failed");

    // Ciphertext != plaintext
    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "DATA: ciphertext must differ from plaintext");

    // Sin fuga en el frame completo
    TEST_ASSERT_TRUE_MESSAGE(noPlaintextLeak(frame, frameLen, payload, ptLen),
                             "DATA: plaintext must not appear in encrypted frame");

    // Descifrado correcto
    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen),
                             "DATA: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "DATA: decrypted payload must match original");
}

// ── DATA_FRAG ────────────────────────────────────────────────────────────────
void test_data_frag_frame_encrypted_and_decryptable(void) {
    // Payload = frag header (4B) + IP fragment data
    MeshFragHeader fh = {0xABCD, 0, 3};   // fragId, index, total
    uint8_t fragPayload[MESH_FRAG_HEADER_SIZE + 12] = {};
    LinkLayer::serializeFragHeader(fragPayload, fh);
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
                             0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0};
    memcpy(fragPayload + MESH_FRAG_HEADER_SIZE, data, sizeof(data));
    const size_t ptLen = sizeof(fragPayload);

    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];
    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA_FRAG, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 2,
        g_linkKeyA, fragPayload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "DATA_FRAG: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, fragPayload, ptLen) == 0,
                              "DATA_FRAG: ciphertext must differ from plaintext");
    TEST_ASSERT_TRUE_MESSAGE(noPlaintextLeak(frame, frameLen, fragPayload, ptLen),
                             "DATA_FRAG: plaintext must not appear in encrypted frame");

    uint8_t recovered[sizeof(fragPayload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen),
                             "DATA_FRAG: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(fragPayload, recovered, ptLen,
                                     "DATA_FRAG: decrypted payload must match original");
}

// ── KEY_EXCH_CONFIRM ─────────────────────────────────────────────────────────
void test_key_exch_confirm_encrypted_and_decryptable(void) {
    // Payload: challenge = nonceA XOR nonceB (spec §4.3), here simulated with 16 fixed bytes
    uint8_t nonceA[16], nonceB[16];
    memset(nonceA, 0xAA, sizeof(nonceA));
    memset(nonceB, 0x55, sizeof(nonceB));
    uint8_t challenge[16];
    for (int i = 0; i < 16; ++i) challenge[i] = nonceA[i] ^ nonceB[i];
    const size_t ptLen = sizeof(challenge);

    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];
    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::KEY_EXCH_CONFIRM, Protocol::MESH_INTERNAL,
        MAC_A, MAC_B, networkId, 1, 3,
        g_linkKeyA, challenge, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "KEY_EXCH_CONFIRM: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, challenge, ptLen) == 0,
                              "KEY_EXCH_CONFIRM: ciphertext must differ from plaintext");
    TEST_ASSERT_TRUE_MESSAGE(noPlaintextLeak(frame, frameLen, challenge, ptLen),
                             "KEY_EXCH_CONFIRM: challenge must not appear unencrypted");

    uint8_t recovered[sizeof(challenge)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen),
                             "KEY_EXCH_CONFIRM: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(challenge, recovered, ptLen,
                                     "KEY_EXCH_CONFIRM: recovered challenge must match");
}

// ── PROXY ────────────────────────────────────────────────────────────────────
void test_proxy_frame_encrypted_and_decryptable(void) {
    // Payload: destination IP (4B) + proxied data
    const uint8_t payload[] = {192, 168, 1, 100, 0x08, 0x00, 0x45, 0x00,
                                0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x40, 0x06};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::PROXY, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 4,
        g_linkKeyA, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "PROXY: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "PROXY: ciphertext must differ from plaintext");
    TEST_ASSERT_TRUE_MESSAGE(noPlaintextLeak(frame, frameLen, payload, ptLen),
                             "PROXY: plaintext must not appear in encrypted frame");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen),
                             "PROXY: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "PROXY: decrypted payload must match original");
}

// ---------------------------------------------------------------------------
// ── BROADCAST FRAMES encrypted with NetworkKey ───────────────────────────
// ---------------------------------------------------------------------------

// ── ROUTE_ADV ────────────────────────────────────────────────────────────────
void test_route_adv_encrypted_and_decryptable(void) {
    // Payload: 1 entrada de 12 bytes (destIP 4B + destMAC 6B + hopCount 1B + metric 1B)
    const uint8_t payload[12] = {192, 168, 1, 10,            // destIP
                                  0xAA, 0x11, 0x22, 0x33, 0x44, 0x55,  // destMAC
                                  0x01,                       // hopCount
                                  0x01};                      // metric
    const size_t  ptLen       = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::ROUTE_ADV, Protocol::MESH_INTERNAL,
        MAC_A, MAC_BC, networkId, 1, 10,
        g_networkKey, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "ROUTE_ADV: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "ROUTE_ADV: ciphertext must differ from plaintext");
    TEST_ASSERT_TRUE_MESSAGE(noPlaintextLeak(frame, frameLen, payload, ptLen),
                             "ROUTE_ADV: route data must not appear in plaintext");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "ROUTE_ADV: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "ROUTE_ADV: decrypted payload must match original");
}

// ── ROUTE_WITHDRAW ───────────────────────────────────────────────────────────
void test_route_withdraw_encrypted_and_decryptable(void) {
    // Payload: MAC del destino retirado (6B)
    const uint8_t payload[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
    const size_t  ptLen      = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::ROUTE_WITHDRAW, Protocol::MESH_INTERNAL,
        MAC_A, MAC_BC, networkId, 1, 11,
        g_networkKey, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "ROUTE_WITHDRAW: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "ROUTE_WITHDRAW: ciphertext must differ from plaintext");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "ROUTE_WITHDRAW: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "ROUTE_WITHDRAW: decrypted payload must match original");
}

// ── ARP_QUERY ────────────────────────────────────────────────────────────────
void test_arp_query_encrypted_and_decryptable(void) {
    // Payload: IP solicitada (4B)
    const uint8_t payload[4] = {10, 0, 0, 42};
    const size_t  ptLen      = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::ARP_QUERY, Protocol::MESH_INTERNAL,
        MAC_A, MAC_BC, networkId, 1, 20,
        g_networkKey, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "ARP_QUERY: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "ARP_QUERY: IP must not appear unencrypted");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "ARP_QUERY: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "ARP_QUERY: decrypted payload must match original");
}

// ── ARP_REPLY ────────────────────────────────────────────────────────────────
void test_arp_reply_encrypted_and_decryptable(void) {
    // Payload: IP(4B) + MAC(6B)
    const uint8_t payload[10] = {10, 0, 0, 42,
                                  0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
    const size_t  ptLen       = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::ARP_REPLY, Protocol::MESH_INTERNAL,
        MAC_B, MAC_BC, networkId, 1, 21,
        g_networkKey, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "ARP_REPLY: buildEncryptedFrame failed");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "ARP_REPLY: IP+MAC must not appear unencrypted");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "ARP_REPLY: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "ARP_REPLY: decrypted payload must match original");
}

// ── SERVICE_QUERY ─────────────────────────────────────────────────────────────
void test_service_query_encrypted_and_decryptable(void) {
    // Payload: nombre del servicio (SERVICE_NAME_MAX = 16B, rellenado con 0)
    uint8_t payload[SERVICE_NAME_MAX] = {};
    strncpy((char*)payload, "_mqtt._tcp", SERVICE_NAME_MAX - 1);
    const size_t ptLen = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::SERVICE_QUERY, Protocol::MESH_INTERNAL,
        MAC_A, MAC_BC, networkId, 1, 30,
        g_networkKey, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "SERVICE_QUERY: buildEncryptedFrame failed");

    // The service name must not appear in plaintext inside the frame
    TEST_ASSERT_TRUE_MESSAGE(
        noPlaintextLeak(frame + MESH_HEADER_SIZE, frameLen - MESH_HEADER_SIZE,
                        (const uint8_t*)"_mqtt._tcp", strlen("_mqtt._tcp")),
        "SERVICE_QUERY: service name must not appear unencrypted");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "SERVICE_QUERY: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "SERVICE_QUERY: decrypted payload must match original");
}

// ── SERVICE_REPLY ─────────────────────────────────────────────────────────────
void test_service_reply_encrypted_and_decryptable(void) {
    // Payload: nombre(16B) + IP(4B) + port(2B) = 22 bytes
    struct __attribute__((packed)) ServiceReplyPayload {
        char     name[SERVICE_NAME_MAX];
        uint8_t  ip[4];
        uint16_t port;
    };
    ServiceReplyPayload svcPayload = {};
    strncpy(svcPayload.name, "_mqtt._tcp", SERVICE_NAME_MAX - 1);
    svcPayload.ip[0] = 10; svcPayload.ip[1] = 0; svcPayload.ip[2] = 0; svcPayload.ip[3] = 1;
    svcPayload.port = 1883;

    const size_t ptLen = sizeof(svcPayload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::SERVICE_REPLY, Protocol::MESH_INTERNAL,
        MAC_B, MAC_BC, networkId, 1, 31,
        g_networkKey, (const uint8_t*)&svcPayload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "SERVICE_REPLY: buildEncryptedFrame failed");

    TEST_ASSERT_TRUE_MESSAGE(
        noPlaintextLeak(frame + MESH_HEADER_SIZE, frameLen - MESH_HEADER_SIZE,
                        (const uint8_t*)"_mqtt._tcp", strlen("_mqtt._tcp")),
        "SERVICE_REPLY: service name must not appear unencrypted");

    uint8_t recovered[sizeof(svcPayload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "SERVICE_REPLY: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&svcPayload, recovered, ptLen,
                                     "SERVICE_REPLY: decrypted payload must match original");
}

// ── KEY_NACK ─────────────────────────────────────────────────────────────────
void test_key_nack_encrypted_and_decryptable(void) {
    // Payload: epoch esperado (1B) + MAC del remitente (6B) = 7 bytes
    const uint8_t payload[7] = {0x02,                               // epoch esperado
                                  0xAA, 0x11, 0x22, 0x33, 0x44, 0x55}; // MAC
    const size_t  ptLen      = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::KEY_NACK, Protocol::MESH_INTERNAL,
        MAC_B, MAC_A, networkId, 1, 40,
        g_networkKey, payload, ptLen,
        frame, sizeof(frame));

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frameLen, "KEY_NACK: buildEncryptedFrame failed");

    uint8_t recovered[sizeof(payload)] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen),
                             "KEY_NACK: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "KEY_NACK: decrypted payload must match original");
}

// ---------------------------------------------------------------------------
// ── PLAINTEXT FRAMES (not encrypted) ────────────────────────────────────
// ---------------------------------------------------------------------------

// ── KEY_EXCH_HELLO ──────────────────────────────────────────────────────────
void test_key_exch_hello_is_plaintext(void) {
    // Payload: pubKey(32B) + nonce(32B), not encrypted
    uint8_t pubA[MESH_ECDH_KEY_SIZE], privA[MESH_ECDH_KEY_SIZE];
    Crypto::generateKeyPair(pubA, privA);

    uint8_t helloPayload[MESH_ECDH_KEY_SIZE * 2] = {};
    memcpy(helloPayload, pubA, MESH_ECDH_KEY_SIZE);
    // nonce: bytes marked with 0xBB for visual inspection
    memset(helloPayload + MESH_ECDH_KEY_SIZE, 0xBB, MESH_ECDH_KEY_SIZE);

    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    // Unencrypted frame: header || payload (no GCM tag)
    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    buildHeader(frame, FrameType::KEY_EXCH_HELLO, Protocol::MESH_INTERNAL,
                MAC_A, MAC_BC, networkId, 0, 50);
    memcpy(frame + MESH_HEADER_SIZE, helloPayload, sizeof(helloPayload));
    size_t frameLen = MESH_HEADER_SIZE + sizeof(helloPayload);

    // The payload must be directly readable (no decryption): the public key
    // we copied must appear in clear in the frame
    const uint8_t* rawPayload = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(pubA, rawPayload, MESH_ECDH_KEY_SIZE,
                                     "KEY_EXCH_HELLO: public key must be readable in plaintext");

    // The nonce must also appear in clear
    const uint8_t expectedNonce[MESH_ECDH_KEY_SIZE] = {};
    memset((void*)expectedNonce, 0xBB, MESH_ECDH_KEY_SIZE);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expectedNonce, rawPayload + MESH_ECDH_KEY_SIZE,
                                     MESH_ECDH_KEY_SIZE,
                                     "KEY_EXCH_HELLO: nonce must be readable in plaintext");

    // The header must be valid
    MeshFrameHeader h = {};
    TEST_ASSERT_TRUE(LinkLayer::deserializeHeader(frame, frameLen, h));
    TEST_ASSERT_EQUAL((uint8_t)FrameType::KEY_EXCH_HELLO, h.frameType);
    (void)frameLen;
}

// ── KEY_EXCH_REPLY ──────────────────────────────────────────────────────────
void test_key_exch_reply_is_plaintext(void) {
    uint8_t pubB[MESH_ECDH_KEY_SIZE], privB[MESH_ECDH_KEY_SIZE];
    Crypto::generateKeyPair(pubB, privB);

    uint8_t replyPayload[MESH_ECDH_KEY_SIZE * 2] = {};
    memcpy(replyPayload, pubB, MESH_ECDH_KEY_SIZE);
    memset(replyPayload + MESH_ECDH_KEY_SIZE, 0xCC, MESH_ECDH_KEY_SIZE);

    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    buildHeader(frame, FrameType::KEY_EXCH_REPLY, Protocol::MESH_INTERNAL,
                MAC_B, MAC_A, networkId, 0, 51);
    memcpy(frame + MESH_HEADER_SIZE, replyPayload, sizeof(replyPayload));
    size_t frameLen = MESH_HEADER_SIZE + sizeof(replyPayload);

    const uint8_t* rawPayload = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(pubB, rawPayload, MESH_ECDH_KEY_SIZE,
                                     "KEY_EXCH_REPLY: public key must be readable in plaintext");

    MeshFrameHeader h = {};
    TEST_ASSERT_TRUE(LinkLayer::deserializeHeader(frame, frameLen, h));
    TEST_ASSERT_EQUAL((uint8_t)FrameType::KEY_EXCH_REPLY, h.frameType);
    (void)frameLen;
}

// ── JOIN_BEACON ──────────────────────────────────────────────────────────────
void test_join_beacon_is_plaintext(void) {
    // Payload: channel(1B) + networkId(2B) = 3 bytes, not encrypted
    // (El nodo sin provisionar necesita leerlo antes de tener ninguna clave)
    const uint8_t channel   = 6;
    const uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t payload[3];
    payload[0] = channel;
    payload[1] = (uint8_t)(networkId >> 8);
    payload[2] = (uint8_t)(networkId & 0xFF);

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    buildHeader(frame, FrameType::JOIN_BEACON, Protocol::MESH_INTERNAL,
                MAC_B, MAC_BC, networkId, 0, 60);
    memcpy(frame + MESH_HEADER_SIZE, payload, sizeof(payload));
    size_t frameLen = MESH_HEADER_SIZE + sizeof(payload);

    // Channel and networkId must be readable without decryption
    const uint8_t* rawPayload = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_EQUAL_MESSAGE(channel, rawPayload[0],
                              "JOIN_BEACON: channel must be readable in plaintext");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(payload[1], rawPayload[1],
                                   "JOIN_BEACON: networkId high byte must be readable");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(payload[2], rawPayload[2],
                                   "JOIN_BEACON: networkId low byte must be readable");

    MeshFrameHeader h = {};
    TEST_ASSERT_TRUE(LinkLayer::deserializeHeader(frame, frameLen, h));
    TEST_ASSERT_EQUAL((uint8_t)FrameType::JOIN_BEACON, h.frameType);
    (void)frameLen;
}

// ---------------------------------------------------------------------------
// ── Security properties of encryption ─────────────────────────────────────
// ---------------------------------------------------------------------------

// ── Wrong key → GCM authentication fails ───────────────────────────────────
void test_wrong_key_fails_authentication(void) {
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 70,
        g_linkKeyA, payload, ptLen,
        frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frameLen);

    // Clave equivocada: NetworkKey en lugar de LinkKey
    uint8_t recovered[sizeof(payload)] = {};
    bool decOk = decryptFrame(frame, frameLen, g_networkKey, recovered, ptLen);
    TEST_ASSERT_FALSE_MESSAGE(decOk,
                              "Wrong key must cause GCM authentication failure");
}

// ── Tampered frame → GCM authentication fails ──────────────────────────────
void test_tampered_frame_fails_authentication(void) {
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 71,
        g_linkKeyA, payload, ptLen,
        frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frameLen);

    // Voltear un bit en el ciphertext
    frame[MESH_HEADER_SIZE] ^= 0x01;

    uint8_t recovered[sizeof(payload)] = {};
    bool decOk = decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen);
    TEST_ASSERT_FALSE_MESSAGE(decOk,
                              "Tampered ciphertext must cause GCM authentication failure");
}

// ── Tampered header (AAD) → GCM authentication fails ───────────────────────
void test_tampered_header_aad_fails_authentication(void) {
    const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 72,
        g_linkKeyA, payload, ptLen,
        frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frameLen);

    // Change the sequence number in the header (AAD) without re-encrypting
    frame[21] ^= 0xFF;

    uint8_t recovered[sizeof(payload)] = {};
    bool decOk = decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen);
    TEST_ASSERT_FALSE_MESSAGE(decOk,
                              "Tampered AAD (header) must cause GCM authentication failure");
}

// ── Replay con mismo nonce pero clave correcta reproduce el mismo ciphertext ─
void test_same_nonce_same_ciphertext_determinism(void) {
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame1[MESH_FRAME_MAX_SIZE] = {};
    uint8_t frame2[MESH_FRAME_MAX_SIZE] = {};
    size_t frameLen1 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 2, 100,
        g_linkKeyA, payload, ptLen, frame1, sizeof(frame1));
    size_t frameLen2 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 2, 100,
        g_linkKeyA, payload, ptLen, frame2, sizeof(frame2));

    TEST_ASSERT_EQUAL(frameLen1, frameLen2);
    // AES-GCM with same parameters produces the same ciphertext (deterministic)
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(frame1 + MESH_HEADER_SIZE,
                                     frame2 + MESH_HEADER_SIZE,
                                     ptLen,
                                     "Same key+nonce+plaintext must produce identical ciphertext");
}

// ---------------------------------------------------------------------------
// ── Tests adicionales de fidelidad (recomendaciones del experto) ────────────
// ---------------------------------------------------------------------------

// ── 1. Unique nonce: different seq → different nonce → different ciphertext ─
// Ensures the system does not reuse nonces between consecutive frames.
void test_consecutive_frames_have_different_nonces(void) {
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    // Frame 1: epoch=1, seq=200
    uint8_t frame1[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen1 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 200,
        g_linkKeyA, payload, ptLen, frame1, sizeof(frame1));
    TEST_ASSERT_GREATER_THAN(0, frameLen1);

    // Frame 2: epoch=1, seq=201 (siguiente frame, mismo contenido y clave)
    uint8_t frame2[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen2 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 201,
        g_linkKeyA, payload, ptLen, frame2, sizeof(frame2));
    TEST_ASSERT_GREATER_THAN(0, frameLen2);

    // Los ciphertexts deben ser distintos (nonces distintos → ciphertexts distintos)
    const uint8_t* cipher1 = frame1 + MESH_HEADER_SIZE;
    const uint8_t* cipher2 = frame2 + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher1, cipher2, ptLen) == 0,
                              "Consecutive frames must produce different ciphertexts (unique nonces)");

    // Both frames must be decryptable correctly
    uint8_t rec1[sizeof(payload)] = {}, rec2[sizeof(payload)] = {};
    TEST_ASSERT_TRUE(decryptFrame(frame1, frameLen1, g_linkKeyA, rec1, ptLen));
    TEST_ASSERT_TRUE(decryptFrame(frame2, frameLen2, g_linkKeyA, rec2, ptLen));
    TEST_ASSERT_EQUAL_MEMORY(payload, rec1, ptLen);
    TEST_ASSERT_EQUAL_MEMORY(payload, rec2, ptLen);
}

// ── 2a. Lower bound size: 0-byte payload ──────────────────────────────────
// Only the header (AAD) and the GCM tag exist; there is no ciphertext.
void test_zero_byte_payload_encrypt_decrypt(void) {
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::MESH_INTERNAL,
        MAC_A, MAC_B, networkId, 1, 210,
        g_linkKeyA, nullptr, 0,
        frame, sizeof(frame));

    // frameLen must be exactly MESH_HEADER_SIZE + MESH_GCM_TAG_SIZE
    TEST_ASSERT_EQUAL_MESSAGE(MESH_HEADER_SIZE + MESH_GCM_TAG_SIZE, (int)frameLen,
                              "Zero-byte payload frame must be header + tag only");

    // It must be decryptable (and recover 0 bytes)
    uint8_t dummy[1] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_linkKeyA, dummy, 0),
                             "Zero-byte payload must decrypt successfully");
}

// ── 2b. Upper bound size: payload of MESH_MAX_PAYLOAD bytes ───────────────
void test_max_payload_encrypt_decrypt(void) {
    uint8_t payload[MESH_MAX_PAYLOAD];
    for (size_t i = 0; i < MESH_MAX_PAYLOAD; i++) payload[i] = (uint8_t)(i & 0xFF);
    const size_t ptLen = MESH_MAX_PAYLOAD;

    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 211,
        g_linkKeyA, payload, ptLen, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_MESSAGE(MESH_HEADER_SIZE + MESH_MAX_PAYLOAD + MESH_GCM_TAG_SIZE,
                              (int)frameLen,
                              "Max-payload frame must have expected total size");

    const uint8_t* cipher = frame + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher, payload, ptLen) == 0,
                              "Max-payload: ciphertext must differ from plaintext");
    TEST_ASSERT_TRUE_MESSAGE(noPlaintextLeak(frame, frameLen, payload, ptLen),
                             "Max-payload: plaintext must not appear in frame");

    uint8_t recovered[MESH_MAX_PAYLOAD] = {};
    TEST_ASSERT_TRUE_MESSAGE(decryptFrame(frame, frameLen, g_linkKeyA, recovered, ptLen),
                             "Max-payload: decryption must succeed");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(payload, recovered, ptLen,
                                     "Max-payload: decrypted payload must match original");
}

// ── 3. Tag position integrity ─────────────────────────────────────────────
// Verifies that the GCM tag is exactly at frame[MESH_HEADER_SIZE + ptLen]
// y tiene MESH_GCM_TAG_SIZE bytes de longitud.
void test_tag_position_and_size_in_frame(void) {
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 220,
        g_linkKeyA, payload, ptLen, frame, sizeof(frame));

    TEST_ASSERT_EQUAL((int)(MESH_HEADER_SIZE + ptLen + MESH_GCM_TAG_SIZE), (int)frameLen);

    // Extract the tag from the expected position
    const uint8_t* tagInFrame = frame + MESH_HEADER_SIZE + ptLen;

    // Recifrar independientemente para obtener el tag de referencia
    uint8_t nonce[MESH_GCM_NONCE_SIZE];
    Crypto::buildNonce(1, 220, MAC_A, nonce);
    uint8_t refCipher[sizeof(payload)], refTag[MESH_GCM_TAG_SIZE];
    Crypto::encrypt(g_linkKeyA, nonce,
                    frame, MESH_HEADER_SIZE,
                    payload, ptLen,
                    refCipher, refTag);

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(refTag, tagInFrame, MESH_GCM_TAG_SIZE,
                                     "GCM tag must be located at frame[MESH_HEADER_SIZE + ptLen]");

    // Corrupt only the tag and verify that decryption fails
    uint8_t corrupted[MESH_FRAME_MAX_SIZE];
    memcpy(corrupted, frame, frameLen);
    corrupted[MESH_HEADER_SIZE + ptLen] ^= 0xFF;  // primer byte del tag

    uint8_t rec[sizeof(payload)] = {};
    TEST_ASSERT_FALSE_MESSAGE(decryptFrame(corrupted, frameLen, g_linkKeyA, rec, ptLen),
                              "Corrupted tag must cause authentication failure");
}

// ── 4. Propiedad AAD: mismo plaintext+key+nonce, AAD diferente → tag diferente ─
// Ensures the header (AAD) is cryptographically bound to the ciphertext.
void test_different_aad_produces_different_tag(void) {
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    const size_t  ptLen     = sizeof(payload);

    // Dos headers con frameType diferente (mismo epoch/seq/MACs)
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    uint8_t frame1[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen1 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 3, 230,
        g_linkKeyA, payload, ptLen, frame1, sizeof(frame1));
    TEST_ASSERT_GREATER_THAN(0, frameLen1);

    uint8_t frame2[MESH_FRAME_MAX_SIZE] = {};
    size_t  frameLen2 = buildEncryptedFrame(
        FrameType::PROXY, Protocol::IPv4,   // distinto FrameType → distinta AAD
        MAC_A, MAC_B, networkId, 3, 230,    // mismo nonce (epoch+seq+src)
        g_linkKeyA, payload, ptLen, frame2, sizeof(frame2));
    TEST_ASSERT_GREATER_THAN(0, frameLen2);

    // Los tags deben ser distintos aunque el ciphertext sea el mismo
    const uint8_t* tag1 = frame1 + MESH_HEADER_SIZE + ptLen;
    const uint8_t* tag2 = frame2 + MESH_HEADER_SIZE + ptLen;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(tag1, tag2, MESH_GCM_TAG_SIZE) == 0,
                              "Different AAD must produce different GCM tag");

    // Attempting to decrypt frame1 with frame2's tag must fail:
    // reemplazar el tag en una copia de frame1 con el tag de frame2
    uint8_t tampered[MESH_FRAME_MAX_SIZE];
    memcpy(tampered, frame1, frameLen1);
    memcpy(tampered + MESH_HEADER_SIZE + ptLen, tag2, MESH_GCM_TAG_SIZE);

    uint8_t rec[sizeof(payload)] = {};
    TEST_ASSERT_FALSE_MESSAGE(decryptFrame(tampered, frameLen1, g_linkKeyA, rec, ptLen),
                              "Tag from different AAD must not authenticate another frame");
}

// ── 5. Propiedad nonce: mismo plaintext+key, nonce diferente → ciphertext diferente
void test_different_nonce_produces_different_ciphertext(void) {
    const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const size_t  ptLen     = sizeof(payload);
    uint16_t networkId = ((uint16_t)g_networkId[0] << 8) | g_networkId[1];

    // Misma clave y payload, pero epoch diferente (→ nonce diferente)
    uint8_t frame1[MESH_FRAME_MAX_SIZE] = {};
    size_t  fl1 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 1, 240,
        g_linkKeyA, payload, ptLen, frame1, sizeof(frame1));

    uint8_t frame2[MESH_FRAME_MAX_SIZE] = {};
    size_t  fl2 = buildEncryptedFrame(
        FrameType::DATA, Protocol::IPv4,
        MAC_A, MAC_B, networkId, 2, 240,   // epoch 2 → nonce diferente
        g_linkKeyA, payload, ptLen, frame2, sizeof(frame2));

    TEST_ASSERT_GREATER_THAN(0, fl1);
    TEST_ASSERT_GREATER_THAN(0, fl2);

    const uint8_t* cipher1 = frame1 + MESH_HEADER_SIZE;
    const uint8_t* cipher2 = frame2 + MESH_HEADER_SIZE;
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cipher1, cipher2, ptLen) == 0,
                              "Different nonce must produce different ciphertext");

    // Both must decrypt correctly with their own frame
    uint8_t rec1[sizeof(payload)] = {}, rec2[sizeof(payload)] = {};
    TEST_ASSERT_TRUE(decryptFrame(frame1, fl1, g_linkKeyA, rec1, ptLen));
    TEST_ASSERT_TRUE(decryptFrame(frame2, fl2, g_linkKeyA, rec2, ptLen));
    TEST_ASSERT_EQUAL_MEMORY(payload, rec1, ptLen);
    TEST_ASSERT_EQUAL_MEMORY(payload, rec2, ptLen);
}

// ---------------------------------------------------------------------------
// main / runner
// ---------------------------------------------------------------------------

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    deriveTestKeys();
    UNITY_BEGIN();
    // Unicast frames – LinkKey
    RUN_TEST(test_data_frame_encrypted_and_decryptable);
    RUN_TEST(test_data_frag_frame_encrypted_and_decryptable);
    RUN_TEST(test_key_exch_confirm_encrypted_and_decryptable);
    RUN_TEST(test_proxy_frame_encrypted_and_decryptable);
    // Broadcast frames – NetworkKey
    RUN_TEST(test_route_adv_encrypted_and_decryptable);
    RUN_TEST(test_route_withdraw_encrypted_and_decryptable);
    RUN_TEST(test_arp_query_encrypted_and_decryptable);
    RUN_TEST(test_arp_reply_encrypted_and_decryptable);
    RUN_TEST(test_service_query_encrypted_and_decryptable);
    RUN_TEST(test_service_reply_encrypted_and_decryptable);
    RUN_TEST(test_key_nack_encrypted_and_decryptable);
    // Unencrypted frames
    RUN_TEST(test_key_exch_hello_is_plaintext);
    RUN_TEST(test_key_exch_reply_is_plaintext);
    RUN_TEST(test_join_beacon_is_plaintext);
    // Security properties
    RUN_TEST(test_wrong_key_fails_authentication);
    RUN_TEST(test_tampered_frame_fails_authentication);
    RUN_TEST(test_tampered_header_aad_fails_authentication);
    RUN_TEST(test_same_nonce_same_ciphertext_determinism);
    // Additional fidelity tests
    RUN_TEST(test_consecutive_frames_have_different_nonces);
    RUN_TEST(test_zero_byte_payload_encrypt_decrypt);
    RUN_TEST(test_max_payload_encrypt_decrypt);
    RUN_TEST(test_tag_position_and_size_in_frame);
    RUN_TEST(test_different_aad_produces_different_tag);
    RUN_TEST(test_different_nonce_produces_different_ciphertext);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    deriveTestKeys();
    UNITY_BEGIN();
    // Unicast frames – LinkKey
    RUN_TEST(test_data_frame_encrypted_and_decryptable);
    RUN_TEST(test_data_frag_frame_encrypted_and_decryptable);
    RUN_TEST(test_key_exch_confirm_encrypted_and_decryptable);
    RUN_TEST(test_proxy_frame_encrypted_and_decryptable);
    // Broadcast frames – NetworkKey
    RUN_TEST(test_route_adv_encrypted_and_decryptable);
    RUN_TEST(test_route_withdraw_encrypted_and_decryptable);
    RUN_TEST(test_arp_query_encrypted_and_decryptable);
    RUN_TEST(test_arp_reply_encrypted_and_decryptable);
    RUN_TEST(test_service_query_encrypted_and_decryptable);
    RUN_TEST(test_service_reply_encrypted_and_decryptable);
    RUN_TEST(test_key_nack_encrypted_and_decryptable);
    // Unencrypted frames
    RUN_TEST(test_key_exch_hello_is_plaintext);
    RUN_TEST(test_key_exch_reply_is_plaintext);
    RUN_TEST(test_join_beacon_is_plaintext);
    // Security properties
    RUN_TEST(test_wrong_key_fails_authentication);
    RUN_TEST(test_tampered_frame_fails_authentication);
    RUN_TEST(test_tampered_header_aad_fails_authentication);
    RUN_TEST(test_same_nonce_same_ciphertext_determinism);
    // Additional fidelity tests
    RUN_TEST(test_consecutive_frames_have_different_nonces);
    RUN_TEST(test_zero_byte_payload_encrypt_decrypt);
    RUN_TEST(test_max_payload_encrypt_decrypt);
    RUN_TEST(test_tag_position_and_size_in_frame);
    RUN_TEST(test_different_aad_produces_different_tag);
    RUN_TEST(test_different_nonce_produces_different_ciphertext);
    return UNITY_END();
}
#endif
