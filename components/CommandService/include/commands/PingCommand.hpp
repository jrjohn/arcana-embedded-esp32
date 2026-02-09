#pragma once

#include "ICommand.hpp"
#include "esp_timer.h"
#include <cstring>

namespace Arcana {
namespace Command {

class PingCommand : public ICommand {
public:
    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = Cluster::System;
        rsp.Command = SystemCmd::Ping;
        rsp.Status = kStatusOk;

        // Return timestamp (microseconds since boot) as uint64_t
        int64_t ts = esp_timer_get_time();
        rsp.PayloadLen = sizeof(ts);
        memcpy(rsp.Payload, &ts, sizeof(ts));

        return rsp;
    }
};

} // namespace Command
} // namespace Arcana
