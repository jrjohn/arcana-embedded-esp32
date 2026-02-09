#pragma once

#include "ICommand.hpp"
#include "BleService.hpp"
#include "esp_bt_device.h"
#include <cstring>

namespace Arcana {
namespace Command {

class SetDeviceNameCommand : public ICommand {
public:
    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.Function = FuncCode::SetDeviceName;

        if (request.PayloadLen == 0 || request.PayloadLen > 29) {
            rsp.Status = kStatusInvalidParam;
            return rsp;
        }

        char name[30] = {};
        memcpy(name, request.Payload, request.PayloadLen);
        name[request.PayloadLen] = '\0';

        esp_err_t ret = esp_ble_gap_set_device_name(name);
        if (ret != ESP_OK) {
            rsp.Status = kStatusError;
            return rsp;
        }

        rsp.Status = kStatusOk;
        memcpy(rsp.Payload, name, request.PayloadLen);
        rsp.PayloadLen = request.PayloadLen;
        return rsp;
    }
};

} // namespace Command
} // namespace Arcana
