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

class KeyExchangeManager;

class CommandCodec {
public:
    esp_err_t Init();

    void SetKeyExchangeManager(KeyExchangeManager* mgr) { mKeyExchangeMgr = mgr; }

    // Replace the command key at runtime with the per-device key provisioned
    // during registration. Re-keys both the PSK fallback engine and the
    // KeyExchangeManager. No-op when encryption is disabled.
    void SetKey(const uint8_t key[CryptoEngine::kKeyLen]);

    bool DecodeRequest(CommandSource source, uint16_t connId,
                       const uint8_t* data, size_t len,
                       CommandRequest& out);

    bool EncodeResponse(const CommandResponse& rsp,
                        uint8_t* buf, size_t bufSize, size_t& outLen);

private:
    bool mEncryptionEnabled = false;
    CryptoEngine mCrypto;                       // PSK-based engine
    KeyExchangeManager* mKeyExchangeMgr = nullptr;
    // Serializes the PSK mCrypto engine: DecodeRequest (decrypt) and
    // EncodeResponse (encrypt) run on different tasks (BLE/MQTT event task vs
    // the cmdrsp response task on ESP32-S3), and a single mbedtls_ccm_context is
    // not safe for concurrent use. The session path is already guarded by
    // KeyExchangeManager's own mutex. Created only when encryption is enabled.
    SemaphoreHandle_t mCryptoMutex = nullptr;
};

} // namespace Command
} // namespace Arcana
