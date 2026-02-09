#pragma once

#include "ICommand.hpp"
#include "BleService.hpp"
#include <cstring>

namespace Arcana {
namespace Command {

class BleScanCommand : public ICommand {
public:
    bool IsAsync() const override { return true; }

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.Function = FuncCode::BleScan;

        // Parse scan duration from payload (default 10 seconds)
        uint32_t durationSec = 10;
        if (request.PayloadLen >= sizeof(uint32_t)) {
            memcpy(&durationSec, request.Payload, sizeof(uint32_t));
        }

        if (durationSec == 0 || durationSec > 300) {
            durationSec = 10;
        }

        auto& gap = Ble::BleGap::Instance();
        esp_err_t ret = gap.StartScanning(durationSec);
        if (ret != ESP_OK) {
            rsp.Status = kStatusError;
            return rsp;
        }

        rsp.Status = kStatusOk;
        // Return the actual scan duration used
        memcpy(rsp.Payload, &durationSec, sizeof(uint32_t));
        rsp.PayloadLen = sizeof(uint32_t);
        return rsp;
    }
};

} // namespace Command
} // namespace Arcana
