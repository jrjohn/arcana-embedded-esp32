#include "impl/SensorServiceImpl.hpp"
#include "esp_log.h"

static const char* TAG = "SensorService";

namespace Arcana {
namespace Sensor {

SensorServiceImpl::SensorServiceImpl() {
    output.DataEvents = new Observable<SensorData>("SensorSvc DataEvents", 20, 4096);
    output.ErrorEvents = new Observable<SensorError>("SensorSvc ErrorEvents", 20, 4096);

    ESP_LOGI(TAG, "Created (output Observables allocated)");
}

SensorServiceImpl::~SensorServiceImpl() {
    if (output.DataEvents) {
        delete output.DataEvents;
        output.DataEvents = nullptr;
    }
    if (output.ErrorEvents) {
        delete output.ErrorEvents;
        output.ErrorEvents = nullptr;
    }
}

SensorService& SensorServiceImpl::getInstance() {
    static SensorServiceImpl sInstance;
    return sInstance;
}

esp_err_t SensorServiceImpl::init_HAL() {
#if CONFIG_DHT_SENSOR_GPIO < 0
    // Sensor disabled by config (e.g. DNESP32S3 — no GPIO can reach the
    // U4 DHT socket without colliding with the SPI LCD or the BOOT key).
    // Output observables exist but never fire; downstream null-checks hold.
    ESP_LOGW(TAG, "DHT sensor disabled (DHT_SENSOR_GPIO=-1)");
    return ESP_OK;
#else
    static DhtSensor sensor(
        static_cast<gpio_num_t>(CONFIG_DHT_SENSOR_GPIO),
#ifdef CONFIG_DHT_SENSOR_TYPE_DHT22
        DhtType::DHT22,
#else
        DhtType::DHT11,
#endif
        SensorConfig().WithId(1).WithInterval(CONFIG_DHT_SENSOR_READ_INTERVAL_MS)
    );
    mSensor = &sensor;
    output.Sensor = mSensor;

    ESP_LOGI(TAG, "HAL initialized (GPIO%d)", CONFIG_DHT_SENSOR_GPIO);
    return ESP_OK;
#endif
}

esp_err_t SensorServiceImpl::init() {
    if (!mSensor) {
#if CONFIG_DHT_SENSOR_GPIO < 0
        return ESP_OK;  // sensor disabled — nothing to wire
#else
        return ESP_ERR_INVALID_STATE;
#endif
    }

    // Forward sensor events to service-level observables
    mSensor->OnData([this](const SensorData& data) {
        output.DataEvents->Notify(data);
    });

    mSensor->OnError([this](const SensorError& err) {
        output.ErrorEvents->Notify(err);
    });

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t SensorServiceImpl::start() {
    if (!mSensor) {
#if CONFIG_DHT_SENSOR_GPIO < 0
        return ESP_OK;  // sensor disabled — nothing to start
#else
        return ESP_ERR_INVALID_STATE;
#endif
    }

    esp_err_t err = mSensor->Start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sensor start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void SensorServiceImpl::stop() {
    if (mSensor) {
        mSensor->Stop();
    }
    ESP_LOGI(TAG, "Stopped");
}

} // namespace Sensor
} // namespace Arcana
