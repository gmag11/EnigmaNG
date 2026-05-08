#include "Crypto.h"
#include "LinkLayer.h"
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════
// Platform-specific crypto primitives
// ═══════════════════════════════════════════════════════════════════════

#if !defined(ESP8266)
// ─── ESP32: mbedTLS ──────────────────────────────────────────────────

#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

bool Crypto::generateKeyPair(uint8_t* publicKey, uint8_t* privateKey) {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ecdh_context     ctx;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ecdh_init(&ctx);

    bool ok = false;

    const char* pers = "enigmang_ecdh";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const uint8_t*)pers, strlen(pers));
    if (ret != 0) goto cleanup;

    ret = mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) goto cleanup;

    ret = mbedtls_ecdh_gen_public(
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(grp),
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(d),
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(Q),
        mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_write_binary(
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(d),
        privateKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_write_binary(
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(X),
        publicKey, MESH_ECDH_KEY_SIZE);
    ok = (ret == 0);

cleanup:
    mbedtls_ecdh_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}

bool Crypto::computeSharedSecret(const uint8_t* privateKey, const uint8_t* peerPublicKey,
                                 uint8_t* sharedSecretOut) {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ecdh_context     ctx;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ecdh_init(&ctx);

    bool ok = false;

    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) goto cleanup;

    ret = mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_read_binary(
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(d),
        privateKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_read_binary(
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(X),
        peerPublicKey, MESH_ECDH_KEY_SIZE);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_lset(
        &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh).MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(Z),
        1);
    if (ret != 0) goto cleanup;

    {
        size_t olen = 0;
        ret = mbedtls_ecdh_calc_secret(&ctx, &olen, sharedSecretOut, MESH_ECDH_KEY_SIZE,
                                        mbedtls_ctr_drbg_random, &ctr_drbg);
        ok = (ret == 0 && olen == MESH_ECDH_KEY_SIZE);
    }

cleanup:
    mbedtls_ecdh_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}

bool Crypto::encrypt(const uint8_t* key, const uint8_t* nonce,
                     const uint8_t* aad, size_t aadLen,
                     const uint8_t* plaintext, size_t ptLen,
                     uint8_t* ciphertext, uint8_t* tag) {
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) { mbedtls_gcm_free(&ctx); return false; }

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
    if (ret != 0) { mbedtls_gcm_free(&ctx); return false; }

    ret = mbedtls_gcm_auth_decrypt(&ctx, ctLen,
                                    nonce, MESH_GCM_NONCE_SIZE,
                                    aad, aadLen,
                                    tag, MESH_GCM_TAG_SIZE,
                                    ciphertext, plaintext);

    mbedtls_gcm_free(&ctx);
    return (ret == 0);
}

bool Crypto::hkdf(const uint8_t* ikm, size_t ikmLen,
                  const uint8_t* salt, size_t saltLen,
                  const uint8_t* info, size_t infoLen,
                  uint8_t* okm, size_t okmLen) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return false;
    return mbedtls_hkdf(md, salt, saltLen, ikm, ikmLen, info, infoLen, okm, okmLen) == 0;
}

#else
// ─── ESP8266: BearSSL ────────────────────────────────────────────────

#include <bearssl/bearssl.h>

// Hardware TRNG on ESP8266 — register 0x3FF20E44
static void fillRandom(uint8_t* buf, size_t len) {
    volatile uint32_t* rng = (volatile uint32_t*)0x3FF20E44;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = *rng;
        size_t n = (len - i < 4) ? (len - i) : 4;
        memcpy(buf + i, &r, n);
    }
}

// mbedTLS uses big-endian MPI serialization for Curve25519 keys/points.
// BearSSL uses little-endian (RFC 7748). Reverse bytes for wire compatibility.
static void reverse32(uint8_t* buf) {
    for (int i = 0; i < 16; i++) {
        uint8_t t = buf[i]; buf[i] = buf[31 - i]; buf[31 - i] = t;
    }
}

bool Crypto::generateKeyPair(uint8_t* publicKey, uint8_t* privateKey) {
    // Generate random scalar in little-endian (BearSSL native)
    uint8_t privLE[32];
    fillRandom(privLE, 32);

    // Clamp per Curve25519 spec (RFC 7748 §5)
    privLE[0]  &= 248;
    privLE[31] &= 127;
    privLE[31] |= 64;

    // Compute public key: Q = privLE * G (BearSSL, little-endian in/out)
    uint8_t pubLE[32];
    const br_ec_impl* ec = br_ec_get_default();
    if (ec->mulgen(pubLE, privLE, 32, BR_EC_curve25519) == 0) return false;

    // Convert to big-endian for wire format (matching mbedTLS convention)
    memcpy(privateKey, privLE, 32);
    reverse32(privateKey);
    memcpy(publicKey, pubLE, 32);
    reverse32(publicKey);

    return true;
}

bool Crypto::computeSharedSecret(const uint8_t* privateKey, const uint8_t* peerPublicKey,
                                 uint8_t* sharedSecretOut) {
    // Convert from big-endian wire format to little-endian for BearSSL
    uint8_t privLE[32], pubLE[32];
    memcpy(privLE, privateKey, 32);
    reverse32(privLE);
    memcpy(pubLE, peerPublicKey, 32);
    reverse32(pubLE);

    // Compute shared secret: S = privLE * pubLE
    const br_ec_impl* ec = br_ec_get_default();
    memcpy(sharedSecretOut, pubLE, 32);
    if (ec->mul(sharedSecretOut, 32, privLE, 32, BR_EC_curve25519) == 0) return false;

    // Convert result to big-endian (matching mbedTLS HKDF input)
    reverse32(sharedSecretOut);
    return true;
}

bool Crypto::encrypt(const uint8_t* key, const uint8_t* nonce,
                     const uint8_t* aad, size_t aadLen,
                     const uint8_t* plaintext, size_t ptLen,
                     uint8_t* ciphertext, uint8_t* tag) {
    br_aes_ct_ctr_keys bc;
    br_gcm_context gc;

    br_aes_ct_ctr_init(&bc, key, 16);
    br_gcm_init(&gc, &bc.vtable, br_ghash_ctmul);

    br_gcm_reset(&gc, nonce, MESH_GCM_NONCE_SIZE);
    br_gcm_aad_inject(&gc, aad, aadLen);
    br_gcm_flip(&gc);

    memcpy(ciphertext, plaintext, ptLen);
    br_gcm_run(&gc, 1, ciphertext, ptLen);  // 1 = encrypt

    br_gcm_get_tag(&gc, tag);
    return true;
}

bool Crypto::decrypt(const uint8_t* key, const uint8_t* nonce,
                     const uint8_t* aad, size_t aadLen,
                     const uint8_t* ciphertext, size_t ctLen,
                     const uint8_t* tag, uint8_t* plaintext) {
    br_aes_ct_ctr_keys bc;
    br_gcm_context gc;

    br_aes_ct_ctr_init(&bc, key, 16);
    br_gcm_init(&gc, &bc.vtable, br_ghash_ctmul);

    br_gcm_reset(&gc, nonce, MESH_GCM_NONCE_SIZE);
    br_gcm_aad_inject(&gc, aad, aadLen);
    br_gcm_flip(&gc);

    memcpy(plaintext, ciphertext, ctLen);
    br_gcm_run(&gc, 0, plaintext, ctLen);  // 0 = decrypt

    // Verify tag
    if (!br_gcm_check_tag(&gc, tag)) {
        memset(plaintext, 0, ctLen);
        return false;
    }
    return true;
}

bool Crypto::hkdf(const uint8_t* ikm, size_t ikmLen,
                  const uint8_t* salt, size_t saltLen,
                  const uint8_t* info, size_t infoLen,
                  uint8_t* okm, size_t okmLen) {
    // HKDF-Extract: PRK = HMAC-SHA256(salt, IKM)
    br_hmac_key_context kc;
    br_hmac_context hc;
    uint8_t prk[32];

    br_hmac_key_init(&kc, &br_sha256_vtable, salt, saltLen);
    br_hmac_init(&hc, &kc, 0);
    br_hmac_update(&hc, ikm, ikmLen);
    br_hmac_out(&hc, prk);

    // HKDF-Expand: T(1) || T(2) || ... truncated to okmLen
    br_hmac_key_init(&kc, &br_sha256_vtable, prk, 32);

    uint8_t t[32];
    size_t tLen = 0;
    uint8_t counter = 1;
    size_t offset = 0;

    while (offset < okmLen) {
        br_hmac_init(&hc, &kc, 0);
        if (tLen > 0) br_hmac_update(&hc, t, tLen);
        br_hmac_update(&hc, info, infoLen);
        br_hmac_update(&hc, &counter, 1);
        br_hmac_out(&hc, t);
        tLen = 32;

        size_t n = (okmLen - offset < 32) ? (okmLen - offset) : 32;
        memcpy(okm + offset, t, n);
        offset += n;
        counter++;
    }
    return true;
}

#endif // !ESP8266

// ═══════════════════════════════════════════════════════════════════════
// Platform-independent functions
// ═══════════════════════════════════════════════════════════════════════

bool Crypto::deriveNetworkKeys(const char* psk, CryptoKeys& keys) {
    size_t pskLen = strlen(psk);

    const uint8_t salt[] = "enigmang";
    const uint8_t infoKey[] = "netkey";
    if (!hkdf((const uint8_t*)psk, pskLen, salt, sizeof(salt) - 1,
              infoKey, sizeof(infoKey) - 1, keys.networkKey, MESH_KEY_SIZE)) {
        return false;
    }

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
    size_t pskLen = strlen(psk);
    uint8_t info[4 + 6 + 6];
    memcpy(info, "link", 4);
    memcpy(info + 4, macA, 6);
    memcpy(info + 10, macB, 6);

    return hkdf(sharedSecret, MESH_ECDH_KEY_SIZE,
                (const uint8_t*)psk, pskLen,
                info, sizeof(info),
                linkKeyOut, MESH_KEY_SIZE);
}

void Crypto::buildNonce(uint8_t epoch, uint16_t sequence, const uint8_t* srcMac,
                        uint8_t* nonceOut) {
    memset(nonceOut, 0, MESH_GCM_NONCE_SIZE);
    nonceOut[0] = epoch;
    nonceOut[1] = (uint8_t)(sequence >> 8);
    nonceOut[2] = (uint8_t)(sequence & 0xFF);
    memcpy(&nonceOut[3], srcMac, 6);
}
