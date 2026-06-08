#include "TsensSensor.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "TsensSensor";

namespace Arcana {
namespace Sensor {

TsensSensor::TsensSensor(const SensorConfig& Config)
    : ObservableSensor(Config)
{
    temperature_sensor_config_t cfg =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);  // expected device range

    esp_err_t err = temperature_sensor_install(&cfg, &mHandle);
    if (err == ESP_OK) {
        err = temperature_sensor_enable(mHandle);
    }

    // Single exit (no early return) so the closing brace's basic block is
    // always reached — gcov otherwise leaves it uncovered on the error path.
    if (err == ESP_OK) {
        mInitialized = true;
        ESP_LOGI(TAG, "Initialized (internal die temperature, -10..80C)");
    } else {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    }
}

TsensSensor::~TsensSensor() {
    if (mHandle) {
        temperature_sensor_disable(mHandle);
        temperature_sensor_uninstall(mHandle);
    }
}

esp_err_t TsensSensor::ReadHardware(SensorData& Data) {
    if (!mInitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    float celsius = 0.0f;
    esp_err_t err = temperature_sensor_get_celsius(mHandle, &celsius);
    if (err != ESP_OK) {
        return err;
    }

    Data.Temperature = celsius;
    Data.Humidity = 0.0f;  // not available on this board
    Data.Value = static_cast<int32_t>(celsius * 100);
    Data.RawValue = Data.Value;
    Data.TimestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    Data.SensorId = GetConfig().SensorId;
    Data.Quality = 70;  // die temperature, not ambient — degrade quality

    ESP_LOGD(TAG, "die temp=%.1fC", celsius);
    return ESP_OK;
}

} // namespace Sensor
} // namespace Arcana
