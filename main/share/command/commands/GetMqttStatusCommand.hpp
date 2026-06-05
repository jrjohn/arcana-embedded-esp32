#pragma once

#include "ICommand.hpp"
#include <cstring>

namespace Arcana {
namespace Command {

class GetMqttStatusCommand : public ICommand {
public:
    // MQTT connected flag is set externally by app_main
    void SetConnected(bool connected) { mConnected = connected; }

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = Cluster::Mqtt;
        rsp.Command = MqttCmd::GetStatus;
        rsp.Status = kStatusOk;

        // Pack: [connected:1]
        rsp.Payload[0] = mConnected ? 1 : 0;
        rsp.PayloadLen = 1;
        return rsp;
    }

private:
    bool mConnected = false;
};

} // namespace Command
} // namespace Arcana
