#pragma once

#include "ICommand.hpp"
#include "ObservableSensor.hpp"
#include <cstring>

namespace Arcana {
namespace Command {

class SetNotifyIntervalCommand : public ICommand {
public:
    explicit SetNotifyIntervalCommand(Sensor::ObservableSensor* sensor) : mSensor(sensor) {}

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = Cluster::Sensor;
        rsp.Command = SensorCmd::SetNotifyInterval;

        if (!mSensor || request.PayloadLen < sizeof(uint32_t)) {
            rsp.Status = kStatusInvalidParam;
            return rsp;
        }

        uint32_t intervalMs = 0;
        memcpy(&intervalMs, request.Payload, sizeof(uint32_t));

        if (intervalMs < 100 || intervalMs > 60000) {
            rsp.Status = kStatusInvalidParam;
            return rsp;
        }

        Sensor::SensorConfig cfg = mSensor->GetConfig();
        cfg.ReadIntervalMs = intervalMs;
        mSensor->SetConfig(cfg);

        rsp.Status = kStatusOk;
        memcpy(rsp.Payload, &intervalMs, sizeof(uint32_t));
        rsp.PayloadLen = sizeof(uint32_t);
        return rsp;
    }

private:
    Sensor::ObservableSensor* mSensor;
};

} // namespace Command
} // namespace Arcana
