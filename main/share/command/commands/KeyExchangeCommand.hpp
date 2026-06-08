#pragma once

#include "ICommand.hpp"
#include "KeyExchangeManager.hpp"
#include <cstring>

namespace Arcana {
namespace Command {

class KeyExchangeCommand : public ICommand {
public:
    explicit KeyExchangeCommand(KeyExchangeManager* mgr) : mMgr(mgr) {}

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = Cluster::Security;
        rsp.Command = SecurityCmd::KeyExchange;

        if (!mMgr) {
            rsp.Status = kStatusError;
            return rsp;
        }

        // Validate: payload must be 64 bytes (client public key x||y)
        if (request.PayloadLen != KeyExchangeManager::kPubKeyLen) {
            rsp.Status = kStatusInvalidParam;
            return rsp;
        }

        uint8_t serverPub[KeyExchangeManager::kPubKeyLen];
        uint8_t authTag[KeyExchangeManager::kAuthTagLen];

        if (!mMgr->PerformKeyExchange(request.Source, request.ConnectionId,
                                       request.Payload, serverPub, authTag)) {
            rsp.Status = kStatusError;
            return rsp;
        }

        // Response payload: [serverPub:64][authTag:32] = 96 bytes
        static constexpr size_t kRspLen = KeyExchangeManager::kPubKeyLen +
                                          KeyExchangeManager::kAuthTagLen;
        memcpy(rsp.Payload, serverPub, KeyExchangeManager::kPubKeyLen);
        memcpy(rsp.Payload + KeyExchangeManager::kPubKeyLen, authTag,
               KeyExchangeManager::kAuthTagLen);
        rsp.PayloadLen = kRspLen;
        rsp.Status = kStatusOk;

        return rsp;
    }

private:
    KeyExchangeManager* mMgr;
};

} // namespace Command
} // namespace Arcana
