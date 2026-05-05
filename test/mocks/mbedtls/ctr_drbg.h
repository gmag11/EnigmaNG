#pragma once
// mbedtls/ctr_drbg.h stub for native unit test builds.
#include <stdint.h>
#include <string.h>

typedef struct { int dummy; } mbedtls_ctr_drbg_context;

inline void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context* ctx)  { (void)ctx; }
inline void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context* ctx)  { (void)ctx; }

inline int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context*,
                                   int (*)(void*, unsigned char*, size_t),
                                   void*, const unsigned char*, size_t) { return 0; }

inline int mbedtls_ctr_drbg_random(void*, unsigned char* output, size_t len) {
    memset(output, 0, len);
    return 0;
}
