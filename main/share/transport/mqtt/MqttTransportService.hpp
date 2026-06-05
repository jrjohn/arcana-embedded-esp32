#pragma once

#include "MqttTypes.hpp"
#include "SensorTypes.hpp"
#include "Observable.hpp"
#include "mqtt_client.h"
#include "esp_err.h"

namespace Arcana {
namespace Mqtt {

class MqttTransportService {
public:
    struct Input {
        Observable<Sensor::SensorData>* SensorDataEvents = nullptr;
    };

    struct Output {
        Observable<MqttCommandEvent>* CommandEvents = nullptr;
        Observable<MqttConnectionStatus>* ConnectionStatus = nullptr;
    };

    Input input;
    Output output;

    virtual ~MqttTransportService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    virtual esp_err_t publish(const char* topic, const uint8_t* data, size_t len, int qos = 1) = 0;
    virtual esp_mqtt_client_handle_t clientHandle() = 0;
};

} // namespace Mqtt
} // namespace Arcana
