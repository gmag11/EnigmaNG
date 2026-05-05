#pragma once
// mbedtls/hkdf.h stub for native unit test builds.
#include "md.h"
#include <stdint.h>

inline int mbedtls_hkdf(const mbedtls_md_info_t*, const uint8_t*, size_t,
                         const uint8_t*, size_t, const uint8_t*, size_t,
                         uint8_t* okm, size_t okm_len) {
    if (okm) __builtin_memset(okm, 0, okm_len);
    return 0;
}
