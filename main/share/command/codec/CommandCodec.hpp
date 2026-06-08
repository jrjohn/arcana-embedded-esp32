#pragma once

#include "CommandTypes.hpp"
#include "CryptoEngine.hpp"
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

    bool DecodeRequest(CommandSource source, uint16_t connId,
                       const uint8_t* data, size_t len,
                       CommandRequest& out);

    bool EncodeResponse(const CommandResponse& rsp,
                        uint8_t* buf, size_t bufSize, size_t& outLen);

private:
    bool mEncryptionEnabled = false;
    CryptoEngine mCrypto;                       // PSK-based engine
    KeyExchangeManager* mKeyExchangeMgr = nullptr;
};

} // namespace Command
} // namespace Arcana
