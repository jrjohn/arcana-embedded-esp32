#include "CryptoEngine.hpp"  // pulls in mbedtls/private/ccm.h with the right define
#include "esp_log.h"
#include "mbedtls/private/sha256.h"
#include <cstring>

static const char* TAG = "CryptoEngine";

namespace Arcana {
namespace Command {

CryptoEngine::~CryptoEngine() {
    if (mInitialized) {
        mbedtls_ccm_free(&mCtx);
    }
}

esp_err_t CryptoEngine::Init(const uint8_t key[kKeyLen]) {
    if (mInitialized) {
        mbedtls_ccm_free(&mCtx);
        mInitialized = false;
    }
    mbedtls_ccm_init(&mCtx);

    int ret = mbedtls_ccm_setkey(&mCtx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ccm_setkey failed: -0x%04x", (unsigned)-ret);
        mbedtls_ccm_free(&mCtx);
        return ESP_FAIL;
    }

    // Derive nonce prefix: SHA256(key || "ARCANA")[0..8]
    uint8_t hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);          // 0 = SHA-256
    mbedtls_sha256_update(&sha, key, kKeyLen);
    const uint8_t salt[] = "ARCANA";
    mbedtls_sha256_update(&sha, salt, 6);    // exclude null terminator
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);

    memcpy(mNoncePrefix, hash, kNoncePrefixLen);

    mTxCounter = 0;
    mRxCounter = 0;
    mRxCounterInitialized = false;
    mInitialized = true;

    ESP_LOGI(TAG, "Initialized (tag=%zu bytes, nonce=%zu bytes)", kTagLen, kNonceLen);
    return ESP_OK;
}

void CryptoEngine::BuildNonce(uint32_t counter, uint8_t nonce[kNonceLen]) const {
    memcpy(nonce, mNoncePrefix, kNoncePrefixLen);
    nonce[kNoncePrefixLen + 0] = static_cast<uint8_t>(counter & 0xFF);
    nonce[kNoncePrefixLen + 1] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    nonce[kNoncePrefixLen + 2] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    nonce[kNoncePrefixLen + 3] = static_cast<uint8_t>((counter >> 24) & 0xFF);
}

bool CryptoEngine::Encrypt(const uint8_t* plain, size_t plainLen,
                            uint8_t* out, size_t outBufSize, size_t& outLen) {
    if (!mInitialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    size_t needed = kCounterLen + plainLen + kTagLen;
    if (outBufSize < needed) {
        ESP_LOGW(TAG, "Output buffer too small: need %zu, have %zu", needed, outBufSize);
        return false;
    }

    // LCOV_EXCL_START — IEC 62304 §5.5.3 defensive guard. Triggering this
    // branch in tests would require ~4 billion successful Encrypt() calls
    // to exhaust the 32-bit counter, which is impractical. The guard
    // prevents CCM nonce reuse on production hardware that runs for years.
    if (mTxCounter == UINT32_MAX) {
        ESP_LOGE(TAG, "TX counter exhausted, refusing encryption (nonce reuse risk)");
        return false;
    }
    // LCOV_EXCL_STOP

    uint32_t counter = mTxCounter++;

    // Write counter (LE) to output
    out[0] = static_cast<uint8_t>(counter & 0xFF);
    out[1] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((counter >> 24) & 0xFF);

    uint8_t nonce[kNonceLen];
    BuildNonce(counter, nonce);

    uint8_t* ciphertext = out + kCounterLen;
    uint8_t* tag = out + kCounterLen + plainLen;

    int ret = mbedtls_ccm_encrypt_and_tag(&mCtx, plainLen,
                                           nonce, kNonceLen,
                                           nullptr, 0,          // no AAD
                                           plain, ciphertext,
                                           tag, kTagLen);
    if (ret != 0) {
        ESP_LOGE(TAG, "Encrypt failed: -0x%04x", (unsigned)-ret);
        return false;
    }

    outLen = needed;
    return true;
}

bool CryptoEngine::Decrypt(const uint8_t* in, size_t inLen,
                            uint8_t* plain, size_t plainBufSize, size_t& plainLen) {
    if (!mInitialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (inLen < kOverhead) {
        ESP_LOGW(TAG, "Input too short for decryption: %zu", inLen);
        return false;
    }

    size_t ciphertextLen = inLen - kOverhead;
    if (plainBufSize < ciphertextLen) {
        ESP_LOGW(TAG, "Plain buffer too small: need %zu, have %zu", ciphertextLen, plainBufSize);
        return false;
    }

    // Read counter (LE) from input
    uint32_t counter = static_cast<uint32_t>(in[0])
                     | (static_cast<uint32_t>(in[1]) << 8)
                     | (static_cast<uint32_t>(in[2]) << 16)
                     | (static_cast<uint32_t>(in[3]) << 24);

    // Replay protection: reject if counter not strictly increasing
    if (mRxCounterInitialized && counter <= mRxCounter) {
        ESP_LOGW(TAG, "Replay detected: counter=%lu <= last=%lu",
                 static_cast<unsigned long>(counter),
                 static_cast<unsigned long>(mRxCounter));
        return false;
    }

    uint8_t nonce[kNonceLen];
    BuildNonce(counter, nonce);

    const uint8_t* ciphertext = in + kCounterLen;
    const uint8_t* tag = in + kCounterLen + ciphertextLen;

    int ret = mbedtls_ccm_auth_decrypt(&mCtx, ciphertextLen,
                                        nonce, kNonceLen,
                                        nullptr, 0,             // no AAD
                                        ciphertext, plain,
                                        tag, kTagLen);
    if (ret != 0) {
        ESP_LOGW(TAG, "Decrypt/auth failed: -0x%04x (counter=%lu)", (unsigned)-ret,
                 static_cast<unsigned long>(counter));
        return false;
    }

    // Update RX counter watermark only after successful decrypt+auth
    mRxCounter = counter;
    mRxCounterInitialized = true;

    plainLen = ciphertextLen;
    return true;
}

bool CryptoEngine::HexToKey(const char* hex, uint8_t key[kKeyLen]) {
    if (!hex || strlen(hex) != kKeyLen * 2) {
        return false;
    }
    for (size_t i = 0; i < kKeyLen; ++i) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        int hiVal = (hi >= '0' && hi <= '9') ? hi - '0' :
                    (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 :
                    (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
        int loVal = (lo >= '0' && lo <= '9') ? lo - '0' :
                    (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 :
                    (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
        if (hiVal < 0 || loVal < 0) return false;
        key[i] = static_cast<uint8_t>((hiVal << 4) | loVal);
    }
    return true;
}

} // namespace Command
} // namespace Arcana
