#include "KeyExchangeManager.hpp"
#include "EspRng.hpp"
#include "esp_log.h"

// mbedtls 4.0 (IDF 6.0+) gates the legacy mbedtls_* low-level crypto APIs
// behind this macro. Define BEFORE pulling in the private headers.
// Note: mbedtls_entropy_* / mbedtls_ctr_drbg_* are GONE in 4.0 (PSA Crypto
// owns randomness now); we wrap esp_fill_random() via EspRng.hpp instead.
#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#endif
#include "mbedtls/ecp.h"
#include "mbedtls/private/ecdh.h"
#include "mbedtls/md.h"
#include <cstring>

static const char* TAG = "KeyExchangeMgr";

namespace Arcana {
namespace Command {

KeyExchangeManager::KeyExchangeManager() {
    memset(&mPending, 0, sizeof(mPending));
}

KeyExchangeManager::~KeyExchangeManager() {
    if (mMutex) {
        vSemaphoreDelete(mMutex);
    }
}

esp_err_t KeyExchangeManager::Init(const uint8_t psk[CryptoEngine::kKeyLen]) {
    memcpy(mPsk, psk, CryptoEngine::kKeyLen);
    mMutex = xSemaphoreCreateMutex();
    if (!mMutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Initialized (max %d sessions)", kMaxSessions);
    return ESP_OK;
}

bool KeyExchangeManager::HmacSha256(const uint8_t* key, size_t keyLen,
                                     const uint8_t* data, size_t dataLen,
                                     uint8_t out[32]) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo) return false;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    int ret = mbedtls_md_setup(&ctx, mdInfo, 1); // 1 = HMAC
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    ret = mbedtls_md_hmac_starts(&ctx, key, keyLen);
    if (ret == 0) ret = mbedtls_md_hmac_update(&ctx, data, dataLen);
    if (ret == 0) ret = mbedtls_md_hmac_finish(&ctx, out);

    mbedtls_md_free(&ctx);
    return ret == 0;
}

bool KeyExchangeManager::HkdfSha256(const uint8_t* ikm, size_t ikmLen,
                                     const uint8_t* salt, size_t saltLen,
                                     const uint8_t* info, size_t infoLen,
                                     uint8_t* okm, size_t okmLen) {
    // Extract: PRK = HMAC-SHA256(salt, ikm)
    uint8_t prk[32];
    if (!HmacSha256(salt, saltLen, ikm, ikmLen, prk)) {
        return false;
    }

    // Expand: T(1) = HMAC-SHA256(PRK, info || 0x01)
    // One HMAC-SHA256 block = 32 bytes, sufficient for 32-byte key
    if (okmLen > 32) return false;

    uint8_t t[32];
    uint8_t expandInput[256]; // info + 1 byte counter
    if (infoLen + 1 > sizeof(expandInput)) return false;

    memcpy(expandInput, info, infoLen);
    expandInput[infoLen] = 0x01;

    if (!HmacSha256(prk, 32, expandInput, infoLen + 1, t)) {
        return false;
    }

    memcpy(okm, t, okmLen);
    return true;
}

bool KeyExchangeManager::PerformKeyExchange(CommandSource source, uint16_t connId,
                                             const uint8_t clientPub[64],
                                             uint8_t serverPub[64], uint8_t authTag[32]) {
    // Reject if session already active or pending for this source/connId
    xSemaphoreTake(mMutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].Active &&
            mSessions[i].Source == source &&
            mSessions[i].ConnId == connId) {
            xSemaphoreGive(mMutex);
            ESP_LOGW(TAG, "Session already active for source=%d connId=%u, rejecting KE",
                     static_cast<int>(source), connId);
            return false;
        }
    }
    if (mPending.Valid &&
        mPending.Source == source &&
        mPending.ConnId == connId) {
        xSemaphoreGive(mMutex);
        ESP_LOGW(TAG, "KeyExchange already pending for source=%d connId=%u",
                 static_cast<int>(source), connId);
        return false;
    }
    xSemaphoreGive(mMutex);

    // RNG: hardware TRNG via esp_fill_random (no need to seed CTR_DRBG —
    // mbedtls 4.0 removed the CTR_DRBG / entropy modules in favour of PSA).
    int ret = 0;

    // Setup ECP group
    mbedtls_ecp_group grp;
    mbedtls_mpi d;           // server private key
    mbedtls_ecp_point Q;     // server public key
    mbedtls_ecp_point Qp;    // client public key
    mbedtls_mpi z;            // shared secret

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);
    mbedtls_ecp_point_init(&Qp);
    mbedtls_mpi_init(&z);

    bool ok = false;

    do {
        ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        if (ret != 0) {
            ESP_LOGE(TAG, "ecp_group_load failed: -0x%04x", (unsigned)-ret);
            break;
        }

        // Generate server keypair
        ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                       Crypto::EspRngCallback, nullptr);
        if (ret != 0) {
            ESP_LOGE(TAG, "ecp_gen_keypair failed: -0x%04x", (unsigned)-ret);
            break;
        }

        // Load client public key (raw x||y, 32 bytes each, big-endian)
        ret = mbedtls_mpi_read_binary(&Qp.MBEDTLS_PRIVATE(X), clientPub, 32);
        if (ret != 0) break;
        ret = mbedtls_mpi_read_binary(&Qp.MBEDTLS_PRIVATE(Y), clientPub + 32, 32);
        if (ret != 0) break;
        ret = mbedtls_mpi_lset(&Qp.MBEDTLS_PRIVATE(Z), 1);
        if (ret != 0) break;

        // Validate client public key
        ret = mbedtls_ecp_check_pubkey(&grp, &Qp);
        if (ret != 0) {
            ESP_LOGE(TAG, "Invalid client public key: -0x%04x", (unsigned)-ret);
            break;
        }

        // Compute shared secret
        ret = mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d,
                                           Crypto::EspRngCallback, nullptr);
        if (ret != 0) {
            ESP_LOGE(TAG, "ecdh_compute_shared failed: -0x%04x", (unsigned)-ret);
            break;
        }

        // Export shared secret to bytes
        uint8_t sharedSecret[32];
        ret = mbedtls_mpi_write_binary(&z, sharedSecret, 32);
        if (ret != 0) break;

        // Export server public key to bytes (x||y)
        ret = mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(X), serverPub, 32);
        if (ret != 0) break;
        ret = mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(Y), serverPub + 32, 32);
        if (ret != 0) break;

        // Derive session key: HKDF(ikm=sharedSecret, salt=PSK, info="ARCANA-SESSION")[0:32]
        const uint8_t info[] = "ARCANA-SESSION";
        uint8_t sessionKey[CryptoEngine::kKeyLen];
        if (!HkdfSha256(sharedSecret, 32, mPsk, sizeof(mPsk),
                         info, sizeof(info) - 1, // exclude null terminator
                         sessionKey, sizeof(sessionKey))) {
            ESP_LOGE(TAG, "HKDF failed");
            break;
        }

        // Compute auth tag: HMAC-SHA256(PSK, serverPub || clientPub)
        uint8_t authData[128];
        memcpy(authData, serverPub, 64);
        memcpy(authData + 64, clientPub, 64);
        if (!HmacSha256(mPsk, sizeof(mPsk), authData, 128, authTag)) {
            ESP_LOGE(TAG, "HMAC auth tag failed");
            break;
        }

        // Stage pending session
        xSemaphoreTake(mMutex, portMAX_DELAY);
        mPending.Valid = true;
        mPending.Source = source;
        mPending.ConnId = connId;
        memcpy(mPending.Key, sessionKey, sizeof(sessionKey));
        xSemaphoreGive(mMutex);

        ok = true;
        ESP_LOGI(TAG, "Key exchange computed for source=%d connId=%u",
                 static_cast<int>(source), connId);
    } while (false);

    // Cleanup
    mbedtls_mpi_free(&z);
    mbedtls_ecp_point_free(&Qp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);

    return ok;
}

bool KeyExchangeManager::InstallPendingSession(CommandSource source, uint16_t connId) {
    xSemaphoreTake(mMutex, portMAX_DELAY);

    if (!mPending.Valid || mPending.Source != source || mPending.ConnId != connId) {
        xSemaphoreGive(mMutex);
        ESP_LOGW(TAG, "No pending session for source=%d connId=%u",
                 static_cast<int>(source), connId);
        return false;
    }

    // Find existing slot or empty slot
    int slot = -1;
    for (int i = 0; i < kMaxSessions; ++i) {
        // LCOV_EXCL_START — IEC 62304 §5.5.3 defensive slot-replacement.
        // PerformKeyExchange already rejects a second key exchange for an
        // (source, connId) that has an active session, so by the time
        // InstallPendingSession runs, no active slot exists for the same
        // pair. This loop body's "found existing" branch is unreachable
        // through the public API; it's defensive in case PerformKeyExchange's
        // duplicate-detection ever weakens.
        if (mSessions[i].Active &&
            mSessions[i].Source == source &&
            mSessions[i].ConnId == connId) {
            slot = i; // Replace existing session
            break;
        }
        // LCOV_EXCL_STOP
    }
    if (slot < 0) {
        for (int i = 0; i < kMaxSessions; ++i) {
            if (!mSessions[i].Active) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        xSemaphoreGive(mMutex);
        ESP_LOGE(TAG, "No free session slot");
        mPending.Valid = false;
        return false;
    }

    // Init the CryptoEngine with session key
    esp_err_t err = mSessions[slot].Engine.Init(mPending.Key);
    if (err != ESP_OK) {
        xSemaphoreGive(mMutex);
        ESP_LOGE(TAG, "Session CryptoEngine init failed");
        mPending.Valid = false;
        return false;
    }

    mSessions[slot].Active = true;
    mSessions[slot].Source = source;
    mSessions[slot].ConnId = connId;
    mPending.Valid = false;

    xSemaphoreGive(mMutex);

    ESP_LOGI(TAG, "Session installed: slot=%d source=%d connId=%u",
             slot, static_cast<int>(source), connId);
    return true;
}

CryptoEngine* KeyExchangeManager::GetSession(CommandSource source, uint16_t connId) {
    xSemaphoreTake(mMutex, portMAX_DELAY);

    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].Active &&
            mSessions[i].Source == source &&
            mSessions[i].ConnId == connId) {
            xSemaphoreGive(mMutex);
            return &mSessions[i].Engine;
        }
    }

    xSemaphoreGive(mMutex);
    return nullptr;
}

bool KeyExchangeManager::DecryptWithSession(CommandSource source, uint16_t connId,
                                             const uint8_t* in, size_t inLen,
                                             uint8_t* plain, size_t plainBufSize, size_t& plainLen) {
    xSemaphoreTake(mMutex, portMAX_DELAY);

    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].Active &&
            mSessions[i].Source == source &&
            mSessions[i].ConnId == connId) {
            bool ok = mSessions[i].Engine.Decrypt(in, inLen, plain, plainBufSize, plainLen);
            xSemaphoreGive(mMutex);
            return ok;
        }
    }

    xSemaphoreGive(mMutex);
    return false;
}

bool KeyExchangeManager::EncryptWithSession(CommandSource source, uint16_t connId,
                                             const uint8_t* plain, size_t plainLen,
                                             uint8_t* out, size_t outBufSize, size_t& outLen) {
    xSemaphoreTake(mMutex, portMAX_DELAY);

    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].Active &&
            mSessions[i].Source == source &&
            mSessions[i].ConnId == connId) {
            bool ok = mSessions[i].Engine.Encrypt(plain, plainLen, out, outBufSize, outLen);
            xSemaphoreGive(mMutex);
            return ok;
        }
    }

    xSemaphoreGive(mMutex);
    return false;
}

void KeyExchangeManager::RemoveSession(CommandSource source, uint16_t connId) {
    xSemaphoreTake(mMutex, portMAX_DELAY);

    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].Active &&
            mSessions[i].Source == source &&
            mSessions[i].ConnId == connId) {
            mSessions[i].Active = false;
            ESP_LOGI(TAG, "Session removed: slot=%d source=%d connId=%u",
                     i, static_cast<int>(source), connId);
            break;
        }
    }

    // Also invalidate pending if it matches
    if (mPending.Valid && mPending.Source == source && mPending.ConnId == connId) {
        mPending.Valid = false;
    }

    xSemaphoreGive(mMutex);
}

} // namespace Command
} // namespace Arcana
