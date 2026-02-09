#pragma once

#include "ICommand.hpp"
#include "ObservableSensor.hpp"
#include <cstring>

namespace Arcana {
namespace Command {

class GetSensorDataCommand : public ICommand {
public:
    explicit GetSensorDataCommand(Sensor::ObservableSensor* sensor) : mSensor(sensor) {}

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.Function = FuncCode::GetSensorData;

        if (!mSensor) {
            rsp.Status = kStatusError;
            return rsp;
        }

        Sensor::SensorData data = mSensor->GetLastReading();

        // Pack: [temperature:4 float][humidity:4 float][timestamp:4 uint32]
        uint16_t offset = 0;
        memcpy(rsp.Payload + offset, &data.Temperature, sizeof(float));
        offset += sizeof(float);
        memcpy(rsp.Payload + offset, &data.Humidity, sizeof(float));
        offset += sizeof(float);
        memcpy(rsp.Payload + offset, &data.TimestampMs, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        rsp.PayloadLen = offset;
        rsp.Status = kStatusOk;
        return rsp;
    }

private:
    Sensor::ObservableSensor* mSensor;
};

} // namespace Command
} // namespace Arcana
