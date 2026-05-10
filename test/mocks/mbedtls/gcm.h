#pragma once
// mbedtls/gcm.h stub for native unit test builds.
// Implements a simple XOR-stream cipher + MAC so cryptographic properties
// (ciphertext ≠ plaintext, authentication failure on wrong key/tamper) can
// be validated on PC without the real AES-GCM library.
#include <stdint.h>
#include <string.h>

typedef int mbedtls_cipher_id_t;
#define MBEDTLS_CIPHER_ID_AES  2
#define MBEDTLS_GCM_ENCRYPT    1
#define MBEDTLS_GCM_DECRYPT    0
#define MBEDTLS_ERR_GCM_AUTH_FAILED  (-0x0012)

typedef struct {
    uint8_t      key[32];
    unsigned int keyLen;
} mbedtls_gcm_context;

inline void mbedtls_gcm_init(mbedtls_gcm_context* ctx)  { memset(ctx, 0, sizeof(*ctx)); }
inline void mbedtls_gcm_free(mbedtls_gcm_context* ctx)  { (void)ctx; }

inline int mbedtls_gcm_setkey(mbedtls_gcm_context* ctx, mbedtls_cipher_id_t,
                               const uint8_t* key, unsigned int keybits) {
    ctx->keyLen = keybits / 8;
    if (ctx->keyLen > 32) ctx->keyLen = 32;
    memcpy(ctx->key, key, ctx->keyLen);
    return 0;
}

// Internal: compute a simple MAC over key+iv+aad+data.
// Uses different mixing constants per input domain to ensure all fields matter.
static inline void _stub_gcm_mac(const uint8_t* key, unsigned int keyLen,
                                  const uint8_t* iv, size_t iv_len,
                                  const uint8_t* aad, size_t aad_len,
                                  const uint8_t* data, size_t data_len,
                                  uint8_t* tag, size_t tag_len) {
    memset(tag, 0, tag_len);
    for (unsigned int i = 0; i < keyLen;   i++) tag[i % tag_len] ^= key[i]  ^ (uint8_t)(i * 0xB3u + 0x13u);
    if (iv)  for (size_t i = 0; i < iv_len;   i++) tag[i % tag_len] ^= iv[i]  ^ (uint8_t)(i * 0x7Du + 0x29u);
    if (aad) for (size_t i = 0; i < aad_len;  i++) tag[i % tag_len] ^= aad[i] ^ (uint8_t)(i * 0x5Fu + 0x47u);
    if (data)for (size_t i = 0; i < data_len; i++) tag[i % tag_len] ^= data[i]^ (uint8_t)(i * 0x3Bu + 0x59u);
}

inline int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context* ctx, int, size_t length,
                                      const uint8_t* iv, size_t iv_len,
                                      const uint8_t* add, size_t add_len,
                                      const uint8_t* input, uint8_t* output,
                                      size_t tag_len, uint8_t* tag) {
    // XOR-stream encrypt: keystream[i] = key[i%kl] ^ iv[i%il] ^ (i*0x6D+1)
    for (size_t i = 0; i < length; i++) {
        uint8_t ks = ctx->key[i % ctx->keyLen];
        if (iv && iv_len) ks ^= iv[i % iv_len];
        ks ^= (uint8_t)(i * 0x6Du + 1u);
        output[i] = input[i] ^ ks;
    }
    // Compute tag over ciphertext (output) + iv + aad
    if (tag && tag_len)
        _stub_gcm_mac(ctx->key, ctx->keyLen, iv, iv_len, add, add_len,
                      output, length, tag, tag_len);
    return 0;
}

inline int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context* ctx, size_t length,
                                     const uint8_t* iv, size_t iv_len,
                                     const uint8_t* add, size_t add_len,
                                     const uint8_t* tag, size_t tag_len,
                                     const uint8_t* input, uint8_t* output) {
    // Verify tag against ciphertext (input) + iv + aad before decrypting
    if (tag && tag_len) {
        uint8_t expected[16] = {};
        _stub_gcm_mac(ctx->key, ctx->keyLen, iv, iv_len, add, add_len,
                      input, length, expected, tag_len);
        if (memcmp(tag, expected, tag_len) != 0) {
            if (output && length) memset(output, 0, length);
            return MBEDTLS_ERR_GCM_AUTH_FAILED;
        }
    }
    // XOR-stream decrypt (same keystream as encrypt)
    for (size_t i = 0; i < length; i++) {
        uint8_t ks = ctx->key[i % ctx->keyLen];
        if (iv && iv_len) ks ^= iv[i % iv_len];
        ks ^= (uint8_t)(i * 0x6Du + 1u);
        output[i] = input[i] ^ ks;
    }
    return 0;
}
