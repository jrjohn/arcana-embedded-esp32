/**
 * @file Esp32AesCtrCipher.hpp
 * @brief ICipher implementation using ESP32 hardware-accelerated AES-256-CTR
 *
 * Header-only. Uses mbedtls which automatically routes to the ESP32
 * AES hardware accelerator for zero-copy, constant-time encryption.
 *
 * IV construction: [nonce:12 bytes][counter:4 bytes LE] = 16 bytes
 */

#ifndef ARCANA_ESP32_AES_CTR_CIPHER_HPP
#define ARCANA_ESP32_AES_CTR_CIPHER_HPP

#include "ats/ICipher.hpp"
#include "mbedtls/aes.h"
#include <cstring>

namespace arcana {
namespace ats {

class Esp32AesCtrCipher : public ICipher {
public:
    void crypt(const uint8_t key[32], const uint8_t nonce[12],
               uint32_t counter, uint8_t* data, uint16_t len) override {
        // Build 16-byte IV: [nonce:12][counter:4 LE]
        uint8_t iv[16];
        memcpy(iv, nonce, 12);
        iv[12] = (uint8_t)(counter >>  0);
        iv[13] = (uint8_t)(counter >>  8);
        iv[14] = (uint8_t)(counter >> 16);
        iv[15] = (uint8_t)(counter >> 24);

        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, key, 256);

        // AES-CTR: encrypt == decrypt (XOR with keystream)
        size_t nc_off = 0;
        uint8_t stream_block[16] = {};
        mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, iv, stream_block, data, data);

        mbedtls_aes_free(&ctx);
    }

    uint8_t cipherType() const override { return 2; }  // AES-256-CTR
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ESP32_AES_CTR_CIPHER_HPP */
