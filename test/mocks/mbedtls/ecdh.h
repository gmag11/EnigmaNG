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

typedef int mbedtls_mpi_sint;

inline void mbedtls_mpi_free(mbedtls_mpi* X)               { (void)X; }
inline int  mbedtls_mpi_read_binary(mbedtls_mpi*, const uint8_t*, size_t) { return 0; }
inline int  mbedtls_mpi_write_binary(const mbedtls_mpi*, unsigned char* buf, size_t len) {
    if (buf && len) memset(buf, 0, len);
    return 0;
}
inline int  mbedtls_mpi_lset(mbedtls_mpi* X, mbedtls_mpi_sint z) { (void)X; (void)z; return 0; }

// Minimal ECP group stub
typedef struct {
    mbedtls_ecp_group_id id;
} mbedtls_ecp_group;

// ECP point stub (needs X,Y,Z for Qp.X access in Crypto.cpp)
typedef struct {
    mbedtls_mpi X, Y, Z;
} mbedtls_ecp_point;

// Inner ECDH context (mbed_ecdh path accessed by Crypto.cpp)
typedef struct {
    mbedtls_ecp_group grp;  // curve group
    mbedtls_mpi       d;   // private key
    mbedtls_ecp_point Q;   // own public key
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

// gen_public: stub that returns success (keys remain zeros; sufficient for mock testing)
typedef int (*mbedtls_ctr_drbg_random_fn)(void*, unsigned char*, size_t);
inline int mbedtls_ecdh_gen_public(
    mbedtls_ecp_group*, mbedtls_mpi*, mbedtls_ecp_point*,
    mbedtls_ctr_drbg_random_fn, void*) { return 0; }

inline int mbedtls_ecdh_calc_secret(mbedtls_ecdh_context*,
                                     size_t* olen, unsigned char* buf, size_t blen,
                                     int (*)(void*, unsigned char*, size_t), void*) {
    if (buf && blen) memset(buf, 0, blen);
    if (olen) *olen = blen;
    return 0;
}
