#ifndef MESH_CRYPTO_H
#define MESH_CRYPTO_H

#include <Arduino.h>

#define MESH_KEY_SIZE          16   // AES-128
#define MESH_NETWORK_ID_SIZE   2
#define MESH_GCM_NONCE_SIZE   12
#define MESH_ECDH_KEY_SIZE    32   // Curve25519
#define MESH_HKDF_SALT_SIZE   32

struct CryptoKeys {
    uint8_t networkKey[MESH_KEY_SIZE];
    uint8_t networkId[MESH_NETWORK_ID_SIZE];
};

class Crypto {
public:
    // Derive NetworkKey and NetworkID from PSK
    static bool deriveNetworkKeys(const char* psk, CryptoKeys& keys);

    // Derive LinkKey from ECDH shared secret + PSK salt + MACs
    static bool deriveLinkKey(const uint8_t* sharedSecret, const char* psk,
                              const uint8_t* macA, const uint8_t* macB,
                              uint8_t* linkKeyOut);

    // ECDH key generation
    static bool generateKeyPair(uint8_t* publicKey, uint8_t* privateKey);
    static bool computeSharedSecret(const uint8_t* privateKey, const uint8_t* peerPublicKey,
                                    uint8_t* sharedSecretOut);

    // AES-128-GCM encryption
    // nonce is derived from (epoch, sequence, srcMac) - 12 bytes
    // AAD (additional authenticated data) = full 22-byte header
    static bool encrypt(const uint8_t* key, const uint8_t* nonce,
                        const uint8_t* aad, size_t aadLen,
                        const uint8_t* plaintext, size_t ptLen,
                        uint8_t* ciphertext, uint8_t* tag);

    static bool decrypt(const uint8_t* key, const uint8_t* nonce,
                        const uint8_t* aad, size_t aadLen,
                        const uint8_t* ciphertext, size_t ctLen,
                        const uint8_t* tag, uint8_t* plaintext);

    // Build nonce from frame header fields
    static void buildNonce(uint8_t epoch, uint16_t sequence, const uint8_t* srcMac,
                           uint8_t* nonceOut);

    // HKDF-SHA256
    static bool hkdf(const uint8_t* ikm, size_t ikmLen,
                     const uint8_t* salt, size_t saltLen,
                     const uint8_t* info, size_t infoLen,
                     uint8_t* okm, size_t okmLen);
};

#endif // MESH_CRYPTO_H
