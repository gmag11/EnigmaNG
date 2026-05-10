#pragma once
// mbedtls/hkdf.h stub for native unit test builds.
// Returns deterministic, non-zero output that differs per (salt, ikm, info)
// so derived keys are distinct and the crypto tests can validate key separation.
#include "md.h"
#include <stdint.h>

inline int mbedtls_hkdf(const mbedtls_md_info_t*,
                         const uint8_t* salt, size_t salt_len,
                         const uint8_t* ikm,  size_t ikm_len,
                         const uint8_t* info, size_t info_len,
                         uint8_t* okm, size_t okm_len) {
    if (!okm || okm_len == 0) return 0;
    for (size_t i = 0; i < okm_len; i++) {
        // Mix position + all input domains so different (salt,ikm,info) → different okm
        uint8_t v = (uint8_t)((i * 0x6Bu + 0x37u) & 0xFFu);
        if (salt && salt_len > 0) v ^= salt[i % salt_len];
        if (ikm  && ikm_len  > 0) v ^= ikm [i % ikm_len ];
        if (info && info_len > 0) v ^= info[i % info_len];
        okm[i] = v;
    }
    return 0;
}
