#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_err.h"

// mbedtls 4.0 (IDF 6.0+) gates the legacy mbedtls_ccm_* API behind this macro.
// Define BEFORE the private header is pulled in.
#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#endif
#include "mbedtls/private/ccm.h"

namespace Arcana {
namespace Command {

class CryptoEngine {
public:
    CryptoEngine() = default;
    ~CryptoEngine();

    CryptoEngine(const CryptoEngine&) = delete;
    CryptoEngine& operator=(const CryptoEngine&) = delete;

    static constexpr size_t kKeyLen = 32;      // AES-256

    esp_err_t Init(const uint8_t key[kKeyLen]);

    // Encrypt: plaintext -> [counter:4 LE][ciphertext:N][tag:8]
    bool Encrypt(const uint8_t* plain, size_t plainLen,
                 uint8_t* out, size_t outBufSize, size_t& outLen);

    // Decrypt: [counter:4 LE][ciphertext:N][tag:8] -> plaintext
    bool Decrypt(const uint8_t* in, size_t inLen,
                 uint8_t* plain, size_t plainBufSize, size_t& plainLen);

    static constexpr size_t kTagLen = 8;
    static constexpr size_t kCounterLen = 4;
    static constexpr size_t kOverhead = kCounterLen + kTagLen; // 12 bytes

    // Parse 64-char hex string into 32-byte key
    static bool HexToKey(const char* hex, uint8_t key[kKeyLen]);

private:
    static constexpr size_t kNoncePrefixLen = 9;
    static constexpr size_t kNonceLen = kNoncePrefixLen + kCounterLen; // 13

    mbedtls_ccm_context mCtx{};
    uint8_t mNoncePrefix[kNoncePrefixLen]{};
    uint32_t mTxCounter = 0;
    uint32_t mRxCounter = 0;         // Highest accepted RX counter
    bool mRxCounterInitialized = false; // First RX sets baseline
    bool mInitialized = false;

    void BuildNonce(uint32_t counter, uint8_t nonce[kNonceLen]) const;
};

} // namespace Command
} // namespace Arcana
