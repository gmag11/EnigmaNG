#include "Crypto.h"
#include "LinkLayer.h"
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <cstring>

bool Crypto::deriveNetworkKeys(const char* psk, CryptoKeys& keys) {
    size_t pskLen = strlen(psk);

    // NetworkKey = HKDF(IKM=PSK, salt="enigmang", info="netkey", len=16)
    const uint8_t salt[] = "enigmang";
    const uint8_t infoKey[] = "netkey";
    if (!hkdf((const uint8_t*)psk, pskLen, salt, sizeof(salt) - 1,
              infoKey, sizeof(infoKey) - 1, keys.networkKey, MESH_KEY_SIZE)) {
        return false;
    }

    // NetworkID = HKDF(IKM=PSK, salt="enigmang", info="netid", len=2)
    const uint8_t infoId[] = "netid";
    if (!hkdf((const uint8_t*)psk, pskLen, salt, sizeof(salt) - 1,
              infoId, sizeof(infoId) - 1, keys.networkId, MESH_NETWORK_ID_SIZE)) {
        return false;
    }

    return true;
}

bool Crypto::deriveLinkKey(const uint8_t* sharedSecret, const char* psk,
                           const uint8_t* macA, const uint8_t* macB,
                           uint8_t* linkKeyOut) {
    // LinkKey = HKDF(IKM=SharedSecret, salt=PSK, info="link"||macA||macB, len=16)
    size_t pskLen = strlen(psk);

    uint8_t info[4 + 6 + 6]; // "link" + macA + macB
    memcpy(info, "link", 4);
    memcpy(info + 4, macA, 6);
    memcpy(info + 10, macB, 6);

    return hkdf(sharedSecret, MESH_ECDH_KEY_SIZE,
                (const uint8_t*)psk, pskLen,
                info, sizeof(info),
                linkKeyOut, MESH_KEY_SIZE);
}

bool Crypto::generateKeyPair(uint8_t* publicKey, uint8_t* privateKey) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* pers = "enigmang_ecdh";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const uint8_t*)pers, strlen(pers));
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    // Generate random private key (32 bytes)
    ret = mbedtls_ctr_drbg_random(&ctr_drbg, privateKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    // Clamp private key for Curve25519
    privateKey[0] &= 248;
    privateKey[31] &= 127;
    privateKey[31] |= 64;

    // Compute public key = privateKey * basepoint
    // Using mbedtls_ecdh for X25519
    mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);
    mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_CURVE25519);

    ret = mbedtls_mpi_read_binary(&ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(d), privateKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    size_t olen = 0;
    uint8_t buf[32];
    ret = mbedtls_ecdh_calc_secret(&ctx, &olen, buf, sizeof(buf),
                                    mbedtls_ctr_drbg_random, &ctr_drbg);

    // For Curve25519, public key generation is simpler - use raw X25519
    // Fallback: use esp_crypto API or direct Curve25519 basepoint multiplication
    // This will be refined when testing on hardware

    mbedtls_ecdh_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    // TODO: Proper Curve25519 public key computation via mbedtls
    // For now, mark as needing hardware validation
    return true;
}

bool Crypto::computeSharedSecret(const uint8_t* privateKey, const uint8_t* peerPublicKey,
                                 uint8_t* sharedSecretOut) {
    // X25519(privA, pubB) = shared secret
    // Using mbedtls ECDH context for Curve25519
    mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);
    mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_CURVE25519);

    // Load our private key
    int ret = mbedtls_mpi_read_binary(&ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(d),
                                       privateKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    // Load peer's public key
    ret = mbedtls_mpi_read_binary(&ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(X),
                                   peerPublicKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);

    size_t olen = 0;
    ret = mbedtls_ecdh_calc_secret(&ctx, &olen, sharedSecretOut, MESH_ECDH_KEY_SIZE,
                                    mbedtls_ctr_drbg_random, &ctr_drbg);

    mbedtls_ecdh_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return (ret == 0 && olen == MESH_ECDH_KEY_SIZE);
}

bool Crypto::encrypt(const uint8_t* key, const uint8_t* nonce,
                     const uint8_t* aad, size_t aadLen,
                     const uint8_t* plaintext, size_t ptLen,
                     uint8_t* ciphertext, uint8_t* tag) {
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    ret = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
                                     ptLen, nonce, MESH_GCM_NONCE_SIZE,
                                     aad, aadLen,
                                     plaintext, ciphertext,
                                     MESH_GCM_TAG_SIZE, tag);

    mbedtls_gcm_free(&ctx);
    return (ret == 0);
}

bool Crypto::decrypt(const uint8_t* key, const uint8_t* nonce,
                     const uint8_t* aad, size_t aadLen,
                     const uint8_t* ciphertext, size_t ctLen,
                     const uint8_t* tag, uint8_t* plaintext) {
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    ret = mbedtls_gcm_auth_decrypt(&ctx, ctLen,
                                    nonce, MESH_GCM_NONCE_SIZE,
                                    aad, aadLen,
                                    tag, MESH_GCM_TAG_SIZE,
                                    ciphertext, plaintext);

    mbedtls_gcm_free(&ctx);
    return (ret == 0);
}

void Crypto::buildNonce(uint8_t epoch, uint16_t sequence, const uint8_t* srcMac,
                        uint8_t* nonceOut) {
    // Nonce (12 bytes): epoch(1) + sequence(2) + srcMac(6) + padding(3)
    memset(nonceOut, 0, MESH_GCM_NONCE_SIZE);
    nonceOut[0] = epoch;
    nonceOut[1] = (uint8_t)(sequence >> 8);
    nonceOut[2] = (uint8_t)(sequence & 0xFF);
    memcpy(&nonceOut[3], srcMac, 6);
    // bytes 9-11 remain zero (padding)
}

bool Crypto::hkdf(const uint8_t* ikm, size_t ikmLen,
                  const uint8_t* salt, size_t saltLen,
                  const uint8_t* info, size_t infoLen,
                  uint8_t* okm, size_t okmLen) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return false;

    int ret = mbedtls_hkdf(md, salt, saltLen, ikm, ikmLen, info, infoLen, okm, okmLen);
    return (ret == 0);
}
