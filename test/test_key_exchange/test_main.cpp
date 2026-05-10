/**
 * Integration test: Simulated key exchange (ECDH handshake)
 *
 * Scenario:
 *   NodeA and NodeB see each other for the first time on the mesh.
 *   - Each generates an ECDH key pair.
 *   - They exchange public keys (simulated: direct memory assignment).
 *   - Both derive the shared secret with computeSharedSecret().
 *   - Both derive the LinkKey with deriveLinkKey() — results must match.
 *   - Verify the LinkKey allows encryption on one end and decryption on the other.
 *
 * NOTE: On PC we use mbedtls stubs (zeros). Tests verify the protocol FLOW
 * (calls in correct order, correct parameters). Cryptographic correctness
 * is validated on hardware with full mbedtls.
 */

#include <unity.h>
#include <cstring>
#include "Crypto.h"
#include "LinkLayer.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Test 1: Keypair generation flow — both calls must succeed
// ---------------------------------------------------------------------------
void test_keypair_generation_succeeds(void) {
    uint8_t pubA[MESH_ECDH_KEY_SIZE], privA[MESH_ECDH_KEY_SIZE];
    uint8_t pubB[MESH_ECDH_KEY_SIZE], privB[MESH_ECDH_KEY_SIZE];

    TEST_ASSERT_TRUE_MESSAGE(Crypto::generateKeyPair(pubA, privA), "NodeA: generateKeyPair failed");
    TEST_ASSERT_TRUE_MESSAGE(Crypto::generateKeyPair(pubB, privB), "NodeB: generateKeyPair failed");
}

// ---------------------------------------------------------------------------
// Test 2: Shared secret computation — both calls must succeed
//         and produce the same result (ECDH symmetry)
// ---------------------------------------------------------------------------
void test_ecdh_shared_secret_symmetric(void) {
    uint8_t pubA[MESH_ECDH_KEY_SIZE], privA[MESH_ECDH_KEY_SIZE];
    uint8_t pubB[MESH_ECDH_KEY_SIZE], privB[MESH_ECDH_KEY_SIZE];

    Crypto::generateKeyPair(pubA, privA);
    Crypto::generateKeyPair(pubB, privB);

    uint8_t secretA[MESH_ECDH_KEY_SIZE], secretB[MESH_ECDH_KEY_SIZE];
    TEST_ASSERT_TRUE_MESSAGE(Crypto::computeSharedSecret(privA, pubB, secretA),
                             "NodeA: computeSharedSecret failed");
    TEST_ASSERT_TRUE_MESSAGE(Crypto::computeSharedSecret(privB, pubA, secretB),
                             "NodeB: computeSharedSecret failed");

    // ECDH symmetry: DH(privA, pubB) == DH(privB, pubA)
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(secretA, secretB, MESH_ECDH_KEY_SIZE,
                                     "Shared secrets must be identical (ECDH symmetry)");
}

// ---------------------------------------------------------------------------
// Test 3: LinkKey derivation — both nodes obtain the same key
// ---------------------------------------------------------------------------
void test_link_key_derivation_symmetric(void) {
    const char* psk = "TestNetworkPSK!1";

    uint8_t pubA[MESH_ECDH_KEY_SIZE], privA[MESH_ECDH_KEY_SIZE];
    uint8_t pubB[MESH_ECDH_KEY_SIZE], privB[MESH_ECDH_KEY_SIZE];
    Crypto::generateKeyPair(pubA, privA);
    Crypto::generateKeyPair(pubB, privB);

    uint8_t macA[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t macB[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x66};

    uint8_t linkKeyA[MESH_KEY_SIZE], linkKeyB[MESH_KEY_SIZE];

    // NodeA: DH(privA, pubB) → secretA → deriveLinkKey
    uint8_t secretA[MESH_ECDH_KEY_SIZE];
    Crypto::computeSharedSecret(privA, pubB, secretA);
    TEST_ASSERT_TRUE(Crypto::deriveLinkKey(secretA, psk, macA, macB, linkKeyA));

    // NodeB: DH(privB, pubA) → secretB → deriveLinkKey (same MACs, same order)
    uint8_t secretB[MESH_ECDH_KEY_SIZE];
    Crypto::computeSharedSecret(privB, pubA, secretB);
    TEST_ASSERT_TRUE(Crypto::deriveLinkKey(secretB, psk, macA, macB, linkKeyB));

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(linkKeyA, linkKeyB, MESH_KEY_SIZE,
                                     "LinkKeys must be identical after ECDH exchange");
}

// ---------------------------------------------------------------------------
// Test 4: End-to-end encryption with the derived LinkKey
// ---------------------------------------------------------------------------
void test_encrypt_decrypt_with_link_key(void) {
    const char* psk = "TestNetworkPSK!1";

    uint8_t pubA[MESH_ECDH_KEY_SIZE], privA[MESH_ECDH_KEY_SIZE];
    uint8_t pubB[MESH_ECDH_KEY_SIZE], privB[MESH_ECDH_KEY_SIZE];
    Crypto::generateKeyPair(pubA, privA);
    Crypto::generateKeyPair(pubB, privB);

    uint8_t macA[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t macB[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x66};

    uint8_t secretA[MESH_ECDH_KEY_SIZE], secretB[MESH_ECDH_KEY_SIZE];
    Crypto::computeSharedSecret(privA, pubB, secretA);
    Crypto::computeSharedSecret(privB, pubA, secretB);

    uint8_t keyA[MESH_KEY_SIZE], keyB[MESH_KEY_SIZE];
    Crypto::deriveLinkKey(secretA, psk, macA, macB, keyA);
    Crypto::deriveLinkKey(secretB, psk, macA, macB, keyB);

    // Keys must be identical (same inputs via symmetric ECDH)
    TEST_ASSERT_EQUAL_MEMORY(keyA, keyB, MESH_KEY_SIZE);

    // Nonce derivado del header (epoch=1, seq=42, srcMAC=macA)
    uint8_t nonce[MESH_GCM_NONCE_SIZE];
    Crypto::buildNonce(1, 42, macA, nonce);

    const char* message = "Hello EnigmaNG!";
    size_t msgLen = strlen(message);

    // AAD = header simulado de 22 bytes
    uint8_t aad[22] = {};
    uint8_t ciphertext[64], tag[16], plaintext[64];

    // NodeA cifra con su LinkKey
    TEST_ASSERT_TRUE_MESSAGE(
        Crypto::encrypt(keyA, nonce, aad, sizeof(aad),
                        (const uint8_t*)message, msgLen, ciphertext, tag),
        "Encrypt failed");

    // NodeB descifra con su LinkKey (misma clave)
    TEST_ASSERT_TRUE_MESSAGE(
        Crypto::decrypt(keyB, nonce, aad, sizeof(aad),
                        ciphertext, msgLen, tag, plaintext),
        "Decrypt failed");

    plaintext[msgLen] = '\0';
    TEST_ASSERT_EQUAL_STRING_MESSAGE(message, (char*)plaintext,
                                     "Decrypted plaintext must match original");
}

// ---------------------------------------------------------------------------
// Test 5: Deterministic nonce — same epoch+seq+MAC always produces same nonce
// ---------------------------------------------------------------------------
void test_nonce_deterministic(void) {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t nonce1[MESH_GCM_NONCE_SIZE];
    uint8_t nonce2[MESH_GCM_NONCE_SIZE];

    Crypto::buildNonce(3, 1234, mac, nonce1);
    Crypto::buildNonce(3, 1234, mac, nonce2);

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(nonce1, nonce2, MESH_GCM_NONCE_SIZE,
                                     "Same inputs must produce same nonce");

    // Nonce with different epoch must differ
    uint8_t nonce3[MESH_GCM_NONCE_SIZE];
    Crypto::buildNonce(4, 1234, mac, nonce3);
    bool differ = memcmp(nonce1, nonce3, MESH_GCM_NONCE_SIZE) != 0;
    TEST_ASSERT_TRUE_MESSAGE(differ, "Different epoch must produce different nonce");
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_keypair_generation_succeeds);
    RUN_TEST(test_ecdh_shared_secret_symmetric);
    RUN_TEST(test_link_key_derivation_symmetric);
    RUN_TEST(test_encrypt_decrypt_with_link_key);
    RUN_TEST(test_nonce_deterministic);
    return UNITY_END();
}
