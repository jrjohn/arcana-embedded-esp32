#pragma once

#include "CommandTypes.hpp"
#include "CryptoEngine.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>
#include <cstddef>
#include "esp_err.h"

namespace Arcana {
namespace Command {

class KeyExchangeManager {
public:
    static constexpr size_t kPubKeyLen = 64;     // P-256 x||y raw coordinates
    static constexpr size_t kAuthTagLen = 32;    // HMAC-SHA256
    static constexpr int kMaxSessions = 4;       // 3 BLE + 1 MQTT

    KeyExchangeManager();
    ~KeyExchangeManager();

    KeyExchangeManager(const KeyExchangeManager&) = delete;
    KeyExchangeManager& operator=(const KeyExchangeManager&) = delete;

    esp_err_t Init(const uint8_t psk[CryptoEngine::kKeyLen]);

    // ECDH: generate server keypair, derive session key, compute auth tag.
    // Result staged as "pending" (not installed until InstallPendingSession).
    bool PerformKeyExchange(CommandSource source, uint16_t connId,
                            const uint8_t clientPub[64],
                            uint8_t serverPub[64], uint8_t authTag[32]);

    // Install pending session (called AFTER response encrypted with PSK and sent)
    bool InstallPendingSession(CommandSource source, uint16_t connId);

    // Lookup active session CryptoEngine (nullptr if none)
    // WARNING: returned pointer is only safe if caller holds its own lock or
    //          guarantees no concurrent RemoveSession/InstallPendingSession.
    //          Prefer DecryptWithSession / EncryptWithSession for thread safety.
    CryptoEngine* GetSession(CommandSource source, uint16_t connId);

    // Thread-safe decrypt/encrypt: holds mutex during the crypto operation.
    // Returns false if no session found (caller should fall back to PSK).
    bool DecryptWithSession(CommandSource source, uint16_t connId,
                            const uint8_t* in, size_t inLen,
                            uint8_t* plain, size_t plainBufSize, size_t& plainLen);

    bool EncryptWithSession(CommandSource source, uint16_t connId,
                            const uint8_t* plain, size_t plainLen,
                            uint8_t* out, size_t outBufSize, size_t& outLen);

    // Remove session on disconnect
    void RemoveSession(CommandSource source, uint16_t connId);

private:
    struct Session {
        bool Active = false;
        CommandSource Source = CommandSource::Internal;
        uint16_t ConnId = 0;
        CryptoEngine Engine;
    };

    struct PendingSession {
        bool Valid = false;
        CommandSource Source = CommandSource::Internal;
        uint16_t ConnId = 0;
        uint8_t Key[CryptoEngine::kKeyLen] = {};
    };

    Session mSessions[kMaxSessions];
    PendingSession mPending;
    uint8_t mPsk[CryptoEngine::kKeyLen] = {};
    SemaphoreHandle_t mMutex = nullptr;

    // Manual HKDF-SHA256 (MBEDTLS_HKDF_C not enabled in sdkconfig)
    bool HkdfSha256(const uint8_t* ikm, size_t ikmLen,
                    const uint8_t* salt, size_t saltLen,
                    const uint8_t* info, size_t infoLen,
                    uint8_t* okm, size_t okmLen);

    bool HmacSha256(const uint8_t* key, size_t keyLen,
                    const uint8_t* data, size_t dataLen,
                    uint8_t out[32]);
};

} // namespace Command
} // namespace Arcana
