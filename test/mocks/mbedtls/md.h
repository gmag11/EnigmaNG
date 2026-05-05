#pragma once
// mbedtls/md.h stub for native unit test builds.
#include <stdint.h>
#include <string.h>

typedef int mbedtls_md_type_t;
#define MBEDTLS_MD_SHA256 6

typedef struct { mbedtls_md_type_t type; } mbedtls_md_info_t;

inline const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t type) {
    static mbedtls_md_info_t info;
    info.type = type;
    return &info;
}

inline int mbedtls_md_hmac(const mbedtls_md_info_t*, const uint8_t*, size_t,
                            const uint8_t*, size_t, uint8_t* output) {
    (void)output;
    return 0;
}
