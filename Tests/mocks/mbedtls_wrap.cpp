// mbedtls function wrappers for fault injection.
//
// Linked into test_kex_failure_injection via -Wl,--wrap=<name>. The linker
// renames all references to the wrapped name to __wrap_<name> and creates
// __real_<name> aliases. This lets us forward to the real mbedtls
// implementation by default and inject failures via global flags.

#include "mbedtls/md.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ccm.h"
#include <cstdint>

extern "C" {

// ── Test injection flags ───────────────────────────────────────────────────
int g_fail_md_setup           = 0;
int g_fail_md_hmac_starts     = 0;
int g_fail_md_hmac_update     = 0;
int g_fail_md_hmac_finish     = 0;
int g_fail_ecp_group_load     = 0;
int g_fail_ecp_gen_keypair    = 0;
int g_fail_ecp_check_pubkey   = 0;
int g_fail_ecdh_compute_shared = 0;
int g_fail_mpi_read_binary    = 0;
int g_fail_mpi_write_binary   = 0;
int g_fail_ctr_drbg_seed      = 0;
int g_fail_ccm_setkey         = 0;
int g_fail_ccm_encrypt_and_tag = 0;
int g_fail_ccm_auth_decrypt   = 0;

void mbedtls_test_reset_failures() {
    g_fail_md_setup           = 0;
    g_fail_md_hmac_starts     = 0;
    g_fail_md_hmac_update     = 0;
    g_fail_md_hmac_finish     = 0;
    g_fail_ecp_group_load     = 0;
    g_fail_ecp_gen_keypair    = 0;
    g_fail_ecp_check_pubkey   = 0;
    g_fail_ecdh_compute_shared = 0;
    g_fail_mpi_read_binary    = 0;
    g_fail_mpi_write_binary   = 0;
    g_fail_ctr_drbg_seed      = 0;
    g_fail_ccm_setkey         = 0;
    g_fail_ccm_encrypt_and_tag = 0;
    g_fail_ccm_auth_decrypt   = 0;
}

// ── Real symbols (resolved by --wrap) ──────────────────────────────────────
int __real_mbedtls_md_setup(mbedtls_md_context_t*, const mbedtls_md_info_t*, int);
int __real_mbedtls_md_hmac_starts(mbedtls_md_context_t*, const unsigned char*, size_t);
int __real_mbedtls_md_hmac_update(mbedtls_md_context_t*, const unsigned char*, size_t);
int __real_mbedtls_md_hmac_finish(mbedtls_md_context_t*, unsigned char*);
int __real_mbedtls_ecp_group_load(mbedtls_ecp_group*, mbedtls_ecp_group_id);
int __real_mbedtls_ecp_gen_keypair(mbedtls_ecp_group*, mbedtls_mpi*, mbedtls_ecp_point*,
                                    int (*)(void*, unsigned char*, size_t), void*);
int __real_mbedtls_ecp_check_pubkey(const mbedtls_ecp_group*, const mbedtls_ecp_point*);
int __real_mbedtls_ecdh_compute_shared(mbedtls_ecp_group*, mbedtls_mpi*,
                                        const mbedtls_ecp_point*, const mbedtls_mpi*,
                                        int (*)(void*, unsigned char*, size_t), void*);
int __real_mbedtls_mpi_read_binary(mbedtls_mpi*, const unsigned char*, size_t);
int __real_mbedtls_mpi_write_binary(const mbedtls_mpi*, unsigned char*, size_t);
int __real_mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context*,
                                  int (*)(void*, unsigned char*, size_t), void*,
                                  const unsigned char*, size_t);
int __real_mbedtls_ccm_setkey(mbedtls_ccm_context*, mbedtls_cipher_id_t,
                               const unsigned char*, unsigned int);
int __real_mbedtls_ccm_encrypt_and_tag(mbedtls_ccm_context*, size_t,
                                        const unsigned char*, size_t,
                                        const unsigned char*, size_t,
                                        const unsigned char*, unsigned char*,
                                        unsigned char*, size_t);
int __real_mbedtls_ccm_auth_decrypt(mbedtls_ccm_context*, size_t,
                                     const unsigned char*, size_t,
                                     const unsigned char*, size_t,
                                     const unsigned char*, unsigned char*,
                                     const unsigned char*, size_t);

// ── Wrappers ───────────────────────────────────────────────────────────────

int __wrap_mbedtls_md_setup(mbedtls_md_context_t* ctx, const mbedtls_md_info_t* info, int hmac) {
    if (g_fail_md_setup) return -0x5100;
    return __real_mbedtls_md_setup(ctx, info, hmac);
}

int __wrap_mbedtls_md_hmac_starts(mbedtls_md_context_t* ctx, const unsigned char* key, size_t keylen) {
    if (g_fail_md_hmac_starts) return -0x5100;
    return __real_mbedtls_md_hmac_starts(ctx, key, keylen);
}

int __wrap_mbedtls_md_hmac_update(mbedtls_md_context_t* ctx, const unsigned char* input, size_t ilen) {
    if (g_fail_md_hmac_update) return -0x5100;
    return __real_mbedtls_md_hmac_update(ctx, input, ilen);
}

int __wrap_mbedtls_md_hmac_finish(mbedtls_md_context_t* ctx, unsigned char* output) {
    if (g_fail_md_hmac_finish) return -0x5100;
    return __real_mbedtls_md_hmac_finish(ctx, output);
}

int __wrap_mbedtls_ecp_group_load(mbedtls_ecp_group* grp, mbedtls_ecp_group_id id) {
    if (g_fail_ecp_group_load) return -0x4F80;
    return __real_mbedtls_ecp_group_load(grp, id);
}

int __wrap_mbedtls_ecp_gen_keypair(mbedtls_ecp_group* grp, mbedtls_mpi* d, mbedtls_ecp_point* Q,
                                    int (*f_rng)(void*, unsigned char*, size_t), void* p_rng) {
    if (g_fail_ecp_gen_keypair) return -0x4F80;
    return __real_mbedtls_ecp_gen_keypair(grp, d, Q, f_rng, p_rng);
}

int __wrap_mbedtls_ecp_check_pubkey(const mbedtls_ecp_group* grp, const mbedtls_ecp_point* pt) {
    if (g_fail_ecp_check_pubkey) return -0x4F80;
    return __real_mbedtls_ecp_check_pubkey(grp, pt);
}

int __wrap_mbedtls_ecdh_compute_shared(mbedtls_ecp_group* grp, mbedtls_mpi* z,
                                        const mbedtls_ecp_point* Q, const mbedtls_mpi* d,
                                        int (*f_rng)(void*, unsigned char*, size_t), void* p_rng) {
    if (g_fail_ecdh_compute_shared) return -0x4F80;
    return __real_mbedtls_ecdh_compute_shared(grp, z, Q, d, f_rng, p_rng);
}

int __wrap_mbedtls_mpi_read_binary(mbedtls_mpi* X, const unsigned char* buf, size_t buflen) {
    if (g_fail_mpi_read_binary) return -0x0010;
    return __real_mbedtls_mpi_read_binary(X, buf, buflen);
}

int __wrap_mbedtls_mpi_write_binary(const mbedtls_mpi* X, unsigned char* buf, size_t buflen) {
    if (g_fail_mpi_write_binary) return -0x0010;
    return __real_mbedtls_mpi_write_binary(X, buf, buflen);
}

int __wrap_mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context* ctx,
                                  int (*f_entropy)(void*, unsigned char*, size_t),
                                  void* p_entropy,
                                  const unsigned char* custom, size_t len) {
    if (g_fail_ctr_drbg_seed) return -0x0034;
    return __real_mbedtls_ctr_drbg_seed(ctx, f_entropy, p_entropy, custom, len);
}

int __wrap_mbedtls_ccm_setkey(mbedtls_ccm_context* ctx, mbedtls_cipher_id_t cipher,
                               const unsigned char* key, unsigned int keybits) {
    if (g_fail_ccm_setkey) return -0x000D;
    return __real_mbedtls_ccm_setkey(ctx, cipher, key, keybits);
}

int __wrap_mbedtls_ccm_encrypt_and_tag(mbedtls_ccm_context* ctx, size_t length,
                                        const unsigned char* iv, size_t iv_len,
                                        const unsigned char* add, size_t add_len,
                                        const unsigned char* input, unsigned char* output,
                                        unsigned char* tag, size_t tag_len) {
    if (g_fail_ccm_encrypt_and_tag) return -0x000F;
    return __real_mbedtls_ccm_encrypt_and_tag(ctx, length, iv, iv_len, add, add_len,
                                                input, output, tag, tag_len);
}

int __wrap_mbedtls_ccm_auth_decrypt(mbedtls_ccm_context* ctx, size_t length,
                                     const unsigned char* iv, size_t iv_len,
                                     const unsigned char* add, size_t add_len,
                                     const unsigned char* input, unsigned char* output,
                                     const unsigned char* tag, size_t tag_len) {
    if (g_fail_ccm_auth_decrypt) return -0x000F;
    return __real_mbedtls_ccm_auth_decrypt(ctx, length, iv, iv_len, add, add_len,
                                             input, output, tag, tag_len);
}

} // extern "C"
