/*
 * DHT11/DHT22 Sensor Driver Implementation
 *
 * Single-wire protocol with critical section for timing accuracy.
 * Uses fixed-point sampling: delay 35µs after HIGH edge, then read pin level.
 *   - Bit 0 HIGH lasts ~26µs → pin already LOW at 35µs → 0
 *   - Bit 1 HIGH lasts ~70µs → pin still HIGH at 35µs → 1
 *
 * DHT11 data format: 5 bytes = [Humid_Int, Humid_Dec, Temp_Int, Temp_Dec, Checksum]
 * DHT22 data format: 5 bytes = [Humid_Hi, Humid_Lo, Temp_Hi, Temp_Lo, Checksum]
 */

#include "DhtSensor.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "DhtSensor";

namespace Arcana {
namespace Sensor {

/*******************************************************************************
 * Constructor
 ******************************************************************************/

DhtSensor::DhtSensor(gpio_num_t GpioPin, DhtType Type, const SensorConfig& Config)
    : ObservableSensor(Config)
    , mGpioPin(GpioPin)
    , mType(Type)
    , mInitialized(false)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << mGpioPin);
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(mGpioPin, 1);
    mInitialized = true;

    ESP_LOGI(TAG, "DHT%d initialized on GPIO%d",
             mType == DhtType::DHT11 ? 11 : 22, mGpioPin);
}

/*******************************************************************************
 * ReadHardware Override
 ******************************************************************************/

esp_err_t DhtSensor::ReadHardware(SensorData& Data) {
    if (!mInitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t bytes[5] = {0};

    // --- Start signal: pull low >=18ms (not timing-critical) ---
    gpio_set_level(mGpioPin, 0);
    esp_rom_delay_us(20000);

    // --- Enter critical BEFORE releasing line to avoid interrupt gap ---
    // Critical section covers: release → response → 40-bit read (~5ms total)
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    gpio_set_level(mGpioPin, 1);
    esp_rom_delay_us(40);

    esp_err_t err = WaitForResponse();
    if (err == ESP_OK) {
        err = ReadBytes(bytes);
    }

    taskEXIT_CRITICAL(&mux);

    if (err != ESP_OK) {
        return err;
    }

    // Verify checksum
    uint8_t checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (checksum != bytes[4]) {
        ESP_LOGW(TAG, "Checksum mismatch: calc=0x%02x recv=0x%02x "
                 "[%02x %02x %02x %02x %02x]",
                 checksum, bytes[4],
                 bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
        return ESP_ERR_INVALID_CRC;
    }

    // Parse data based on sensor type
    if (mType == DhtType::DHT11) {
        Data.Humidity = static_cast<float>(bytes[0]) + static_cast<float>(bytes[1]) * 0.1f;
        Data.Temperature = static_cast<float>(bytes[2]) + static_cast<float>(bytes[3]) * 0.1f;
    } else {
        uint16_t rawHumid = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
        uint16_t rawTemp = (static_cast<uint16_t>(bytes[2]) << 8) | bytes[3];

        Data.Humidity = rawHumid * 0.1f;

        if (rawTemp & 0x8000) {
            Data.Temperature = -static_cast<float>(rawTemp & 0x7FFF) * 0.1f;
        } else {
            Data.Temperature = rawTemp * 0.1f;
        }
    }

    Data.Value = static_cast<int32_t>(Data.Temperature * 100);
    Data.RawValue = (static_cast<int32_t>(bytes[0]) << 24) |
                    (static_cast<int32_t>(bytes[1]) << 16) |
                    (static_cast<int32_t>(bytes[2]) << 8)  |
                    static_cast<int32_t>(bytes[3]);
    Data.TimestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    Data.SensorId = GetConfig().SensorId;
    Data.Quality = 90;

    ESP_LOGI(TAG, "temp=%.1f°C humid=%.1f%%",
             Data.Temperature, Data.Humidity);

    return ESP_OK;
}

/*******************************************************************************
 * Protocol Implementation
 ******************************************************************************/

esp_err_t DhtSensor::WaitForResponse() {
    // After host releases line, DHT responds:
    //   LOW ~80µs (response) → HIGH ~80µs (preparation) → LOW (first data bit)

    if (WaitForLevel(1, 200) < 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (WaitForLevel(0, 200) < 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (WaitForLevel(1, 200) < 0) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t DhtSensor::ReadBytes(uint8_t Bytes[5]) {
    for (int i = 0; i < 40; i++) {
        // Each bit: LOW ~50µs (start), then HIGH 26-28µs (0) or 70µs (1)

        // Wait for LOW period to end → pin goes HIGH
        if (WaitForLevel(0, 100) < 0) {
            return ESP_ERR_TIMEOUT;
        }

        // Pin is now HIGH. Delay 35µs then sample:
        //   bit 0 HIGH = 26-28µs → already LOW after 35µs
        //   bit 1 HIGH = 70µs   → still HIGH after 35µs
        esp_rom_delay_us(35);

        if (gpio_get_level(mGpioPin) == 1) {
            Bytes[i / 8] |= (1 << (7 - (i % 8)));

            // Wait for remaining HIGH to end before next bit
            if (WaitForLevel(1, 100) < 0) {
                return ESP_ERR_TIMEOUT;
            }
        }
        // If pin is LOW, we're already in next bit's LOW period → loop continues
    }

    return ESP_OK;
}

int32_t DhtSensor::WaitForLevel(int Level, uint32_t TimeoutUs) {
    int64_t deadline = esp_timer_get_time() + TimeoutUs;

    while (gpio_get_level(mGpioPin) == Level) {
        if (esp_timer_get_time() > deadline) {
            return -1;
        }
    }

    return 0;
}

} // namespace Sensor
} // namespace Arcana
