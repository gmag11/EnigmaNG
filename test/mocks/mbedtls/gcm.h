#pragma once
// mbedtls/gcm.h stub for native unit test builds.
#include <stdint.h>
#include <string.h>

typedef int mbedtls_cipher_id_t;
#define MBEDTLS_CIPHER_ID_AES  2
#define MBEDTLS_GCM_ENCRYPT    1
#define MBEDTLS_GCM_DECRYPT    0

typedef struct { int dummy; } mbedtls_gcm_context;

inline void mbedtls_gcm_init(mbedtls_gcm_context* ctx)  { (void)ctx; }
inline void mbedtls_gcm_free(mbedtls_gcm_context* ctx)  { (void)ctx; }

inline int mbedtls_gcm_setkey(mbedtls_gcm_context*, mbedtls_cipher_id_t,
                               const uint8_t*, unsigned int) { return 0; }

inline int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context*, int, size_t length,
                                      const uint8_t*, size_t,
                                      const uint8_t*, size_t,
                                      const uint8_t* input, uint8_t* output,
                                      size_t tag_len, uint8_t* tag) {
    if (output && input && length) memcpy(output, input, length);
    if (tag && tag_len) memset(tag, 0, tag_len);
    return 0;
}

inline int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context*, size_t length,
                                     const uint8_t*, size_t,
                                     const uint8_t*, size_t,
                                     const uint8_t*, size_t,
                                     const uint8_t* input, uint8_t* output) {
    if (output && input && length) memcpy(output, input, length);
    return 0;
}
