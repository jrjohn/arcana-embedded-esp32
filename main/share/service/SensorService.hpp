#pragma once

#include "Observable.hpp"
#include "SensorTypes.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Sensor {

class ObservableSensor;

class SensorService {
public:
    struct Input {};

    struct Output {
        Observable<SensorData>* DataEvents = nullptr;
        Observable<SensorError>* ErrorEvents = nullptr;
        ObservableSensor* Sensor = nullptr;
    };

    Input input;
    Output output;

    virtual ~SensorService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;
};

} // namespace Sensor
} // namespace Arcana
