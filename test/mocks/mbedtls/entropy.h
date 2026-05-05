#pragma once
// mbedtls/entropy.h stub for native unit test builds.
#include <stdint.h>
#include <string.h>

typedef struct { int dummy; } mbedtls_entropy_context;

inline void mbedtls_entropy_init(mbedtls_entropy_context* ctx)  { (void)ctx; }
inline void mbedtls_entropy_free(mbedtls_entropy_context* ctx)  { (void)ctx; }

inline int mbedtls_entropy_func(void*, unsigned char* output, size_t len) {
    memset(output, 0, len);
    return 0;
}
