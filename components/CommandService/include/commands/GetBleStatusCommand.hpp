#pragma once

#include "ICommand.hpp"
#include "BleService.hpp"
#include <cstring>

namespace Arcana {
namespace Command {

class GetBleStatusCommand : public ICommand {
public:
    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.Function = FuncCode::GetBleStatus;
        rsp.Status = kStatusOk;

        // Count connected server clients
        // We expose a simple connected count via payload
        // Pack: [server_conn_count:1]
        auto& server = Ble::BleGattServer::Instance();
        uint8_t connCount = server.GetConnectionCount();

        uint16_t offset = 0;
        rsp.Payload[offset++] = connCount;
        rsp.PayloadLen = offset;
        return rsp;
    }
};

} // namespace Command
} // namespace Arcana
