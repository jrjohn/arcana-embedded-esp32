/*
 * DHT11/DHT22 Sensor - ObservableSensor Subclass
 *
 * Single-wire temperature & humidity sensor driver for ESP32.
 * Uses GPIO bit-banging with critical section for timing accuracy.
 */

#pragma once

#include "ObservableSensor.hpp"
#include "driver/gpio.h"

namespace Arcana {
namespace Sensor {

/**
 * @brief DHT sensor type
 */
enum class DhtType : uint8_t {
    DHT11,
    DHT22
};

/**
 * @brief DHT11/DHT22 sensor driver extending ObservableSensor
 *
 * Overrides ReadHardware() to communicate with DHT sensor
 * via single-wire protocol on a configurable GPIO pin.
 *
 * Usage:
 * @code
 *   DhtSensor sensor(GPIO_NUM_15, DhtType::DHT11,
 *       SensorConfig().WithId(1).WithInterval(2000));
 *
 *   sensor.OnData([](const SensorData& d) {
 *       printf("Temp=%.1f Humid=%.1f\n", d.Temperature, d.Humidity);
 *   });
 *
 *   sensor.Start();
 * @endcode
 *
 * @note DHT11 minimum sampling interval is ~1 second.
 *       DHT22 minimum sampling interval is ~2 seconds.
 */
class DhtSensor : public ObservableSensor {
public:
    /**
     * @brief Construct DHT sensor
     * @param GpioPin GPIO pin connected to DHT data line
     * @param Type DHT11 or DHT22
     * @param Config Sensor configuration
     */
    DhtSensor(gpio_num_t GpioPin, DhtType Type,
              const SensorConfig& Config = SensorConfig());

    ~DhtSensor() override = default;

protected:
    /**
     * @brief Read temperature & humidity from DHT sensor
     */
    esp_err_t ReadHardware(SensorData& Data) override;

private:
    /**
     * @brief Wait for DHT response after start signal
     * @return ESP_OK if DHT responded
     */
    esp_err_t WaitForResponse();

    /**
     * @brief Read 40 bits (5 bytes) from DHT
     * @param[out] Bytes Output buffer (5 bytes)
     * @return ESP_OK on success
     */
    esp_err_t ReadBytes(uint8_t Bytes[5]);

    /**
     * @brief Wait for GPIO to reach expected level with timeout
     * @param Level Expected GPIO level (0 or 1)
     * @param TimeoutUs Timeout in microseconds
     * @return Duration waited in microseconds, or -1 on timeout
     */
    int32_t WaitForLevel(int Level, uint32_t TimeoutUs);

    gpio_num_t mGpioPin;
    DhtType mType;
    bool mInitialized;
};

} // namespace Sensor
} // namespace Arcana
