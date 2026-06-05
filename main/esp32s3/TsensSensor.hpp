/*
 * TsensSensor — ESP32-S3 built-in temperature sensor as an ObservableSensor.
 *
 * The DNESP32S3 has no ambient temp/humidity sensor reachable from a GPIO
 * (the U4 DHT socket only routes to the LCD DC line or the BOOT key), so
 * the S3 board uses the chip's internal die-temperature sensor instead:
 * -10..80°C ±2°C, reads ~5-15°C above ambient under load. Humidity is not
 * available and is reported as 0 with reduced Quality.
 */

#pragma once

#include "ObservableSensor.hpp"
#include "driver/temperature_sensor.h"

namespace Arcana {
namespace Sensor {

class TsensSensor : public ObservableSensor {
public:
    explicit TsensSensor(const SensorConfig& Config = SensorConfig());
    ~TsensSensor() override;

protected:
    /// Read die temperature; humidity is reported as 0 (not available).
    esp_err_t ReadHardware(SensorData& Data) override;

private:
    temperature_sensor_handle_t mHandle = nullptr;
    bool mInitialized = false;
};

} // namespace Sensor
} // namespace Arcana
