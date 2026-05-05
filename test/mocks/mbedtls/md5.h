#pragma once
// mbedtls/md5.h stub for native unit test builds.
#include <stdint.h>
#include <string.h>

typedef struct { int dummy; } mbedtls_md5_context;

inline void mbedtls_md5_init(mbedtls_md5_context*)   {}
inline void mbedtls_md5_free(mbedtls_md5_context*)   {}
inline int  mbedtls_md5_starts(mbedtls_md5_context*) { return 0; }
inline int  mbedtls_md5_update(mbedtls_md5_context*, const uint8_t*, size_t) { return 0; }
inline int  mbedtls_md5_finish(mbedtls_md5_context*, uint8_t output[16]) {
    if (output) memset(output, 0, 16);
    return 0;
}
inline int mbedtls_md5(const uint8_t*, size_t, uint8_t output[16]) {
    if (output) memset(output, 0, 16);
    return 0;
}
