#pragma once

// HMAC-SHA256 / HKDF-SHA256 built directly on the raw mbedtls_sha256 primitive.
//
// Why not mbedtls_md (HMAC) or mbedtls_hkdf? On IDF 6.0 (mbedtls 4.0) the
// mbedtls_md HMAC layer dispatches through PSA Crypto and returns an error when
// psa_crypto_init() has not run — which is the case in this firmware — so any
// HKDF/HMAC built on it fails at runtime. The raw SHA-256 primitive is
// HW-accelerated and PSA-free (it is what CryptoEngine uses for its nonce
// prefix). Shared by KeyExchangeManager (command-channel session keys) and
// RegistrationServiceImpl (registration comm_key) so both derive identically to
// the Python server.
//
// The sha256 calls are bare statements (no return check) to stay
// source-compatible across both toolchains: mbedtls 4.0 returns int, the CI
// host's mbedtls 2.28 deprecated wrappers return void. These are in-memory
// operations on fixed-size buffers that do not fail.

#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#endif
#include "mbedtls/private/sha256.h"
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace Arcana {
namespace Crypto {

inline void HmacSha256(const uint8_t* key, size_t keyLen,
                       const uint8_t* data, size_t dataLen,
                       uint8_t out[32]) {
    constexpr size_t kBlock = 64; // SHA-256 input block size

    uint8_t k0[kBlock] = {};
    if (keyLen > kBlock) {
        mbedtls_sha256_context kc;
        mbedtls_sha256_init(&kc);
        mbedtls_sha256_starts(&kc, 0);
        mbedtls_sha256_update(&kc, key, keyLen);
        mbedtls_sha256_finish(&kc, k0);
        mbedtls_sha256_free(&kc);
    } else {
        memcpy(k0, key, keyLen);
    }

    uint8_t ipad[kBlock], opad[kBlock];
    for (size_t i = 0; i < kBlock; ++i) {
        ipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k0[i] ^ 0x5C);
    }

    mbedtls_sha256_context ctx;
    uint8_t inner[32];

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, ipad, kBlock);
    mbedtls_sha256_update(&ctx, data, dataLen);
    mbedtls_sha256_finish(&ctx, inner);
    mbedtls_sha256_free(&ctx);

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, opad, kBlock);
    mbedtls_sha256_update(&ctx, inner, 32);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

// HKDF-SHA256, single output block (max 32 bytes). okm = expand(extract(ikm)).
inline bool HkdfSha256(const uint8_t* ikm, size_t ikmLen,
                       const uint8_t* salt, size_t saltLen,
                       const uint8_t* info, size_t infoLen,
                       uint8_t* okm, size_t okmLen) {
    if (okmLen > 32) return false;

    // Extract: PRK = HMAC(salt, ikm)
    uint8_t prk[32];
    HmacSha256(salt, saltLen, ikm, ikmLen, prk);

    // Expand: T(1) = HMAC(PRK, info || 0x01)
    uint8_t expand[256];
    if (infoLen + 1 > sizeof(expand)) return false;
    memcpy(expand, info, infoLen);
    expand[infoLen] = 0x01;

    uint8_t t[32];
    HmacSha256(prk, 32, expand, infoLen + 1, t);
    memcpy(okm, t, okmLen);
    return true;
}

} // namespace Crypto
} // namespace Arcana
