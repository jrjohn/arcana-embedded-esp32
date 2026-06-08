#pragma once

#include "SensorService.hpp"
#include "ObservableSensor.hpp"

namespace Arcana {
namespace Sensor {

class SensorServiceImpl : public SensorService {
public:
    static SensorService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

private:
    SensorServiceImpl();
    ~SensorServiceImpl() override;
    SensorServiceImpl(const SensorServiceImpl&) = delete;
    SensorServiceImpl& operator=(const SensorServiceImpl&) = delete;

    ObservableSensor* mSensor = nullptr;
};

} // namespace Sensor
} // namespace Arcana
