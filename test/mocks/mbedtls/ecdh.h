#pragma once
// mbedtls/ecdh.h stub for native unit test builds.
//
// The real ESP32 mbedtls uses MBEDTLS_PRIVATE() to access struct internals.
// For native stubs we define MBEDTLS_PRIVATE(x) as x (no prefix), and
// provide matching struct layouts so Crypto.cpp compiles without changes.
#include <stdint.h>
#include <string.h>

#ifndef MBEDTLS_PRIVATE
#  define MBEDTLS_PRIVATE(member) member
#endif

typedef int mbedtls_ecp_group_id;
#define MBEDTLS_ECP_DP_CURVE25519  6

// Minimal MPI (big number) stub
typedef struct {
    int    s;      // sign
    size_t n;      // limb count
    void*  p;      // limb data (unused in stubs)
} mbedtls_mpi;

inline void mbedtls_mpi_free(mbedtls_mpi* X)               { (void)X; }
inline int  mbedtls_mpi_read_binary(mbedtls_mpi*, const uint8_t*, size_t) { return 0; }

// ECP point stub (needs X,Y,Z for Qp.X access in Crypto.cpp)
typedef struct {
    mbedtls_mpi X, Y, Z;
} mbedtls_ecp_point;

// Inner ECDH context (mbed_ecdh path accessed by Crypto.cpp)
typedef struct {
    mbedtls_mpi       d;   // private key
    mbedtls_ecp_point Qp;  // peer public key
} mbedtls_ecdh_mbed_ctx;

// Variant wrapper (ctx.mbed_ecdh accessed via MBEDTLS_PRIVATE)
typedef struct {
    mbedtls_ecdh_mbed_ctx mbed_ecdh;
} mbedtls_ecdh_var_ctx;

// Public context (ctx field accessed via MBEDTLS_PRIVATE)
typedef struct {
    mbedtls_ecdh_var_ctx ctx;
} mbedtls_ecdh_context;

inline void mbedtls_ecdh_init(mbedtls_ecdh_context* ctx)   { (void)ctx; }
inline void mbedtls_ecdh_free(mbedtls_ecdh_context* ctx)   { (void)ctx; }
inline int  mbedtls_ecdh_setup(mbedtls_ecdh_context*, mbedtls_ecp_group_id) { return 0; }

inline int mbedtls_ecdh_calc_secret(mbedtls_ecdh_context*,
                                     size_t* olen, unsigned char* buf, size_t blen,
                                     int (*)(void*, unsigned char*, size_t), void*) {
    if (buf && blen) memset(buf, 0, blen);
    if (olen) *olen = blen;
    return 0;
}
