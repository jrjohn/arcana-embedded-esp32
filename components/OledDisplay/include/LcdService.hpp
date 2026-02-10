#pragma once

#include "Observable.hpp"
#include "SensorTypes.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Lcd {

class LcdService {
public:
    struct Input {
        Observable<Sensor::SensorData>* SensorDataEvents = nullptr;
    };

    struct Output {};

    Input input;
    Output output;

    virtual ~LcdService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;
};

} // namespace Lcd
} // namespace Arcana
