#pragma once

#include "BleTypes.hpp"
#include "BleGattServer.hpp"
#include "Observable.hpp"
#include "SensorTypes.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Ble {

class BleTransportService {
public:
    struct Input {
        Observable<Sensor::SensorData>* SensorDataEvents = nullptr;
    };

    struct Output {
        Observable<BleConnectionEvent>* ConnectionEvents = nullptr;
        Observable<BleCommandWriteEvent>* CommandWriteEvents = nullptr;
    };

    Input input;
    Output output;

    virtual ~BleTransportService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    virtual BleGattServer& server() = 0;
};

} // namespace Ble
} // namespace Arcana
