#include "impl/BleTransportServiceImpl.hpp"
#include "esp_log.h"

static const char* TAG = "BleTransportService";

namespace Arcana {
namespace Ble {

BleTransportService& BleTransportServiceImpl::getInstance() {
    static BleTransportServiceImpl sInstance;
    return sInstance;
}

esp_err_t BleTransportServiceImpl::init_HAL() {
    auto& bleSvc = BleService::Instance();
    esp_err_t err = bleSvc.Init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BleService init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Wire output pointers
    output.ConnectionEvents = &BleGattServer::Instance().ConnectionEvents();
    output.CommandWriteEvents = &BleGattServer::Instance().CommandWriteEvents();

    ESP_LOGI(TAG, "HAL initialized");
    return ESP_OK;
}

esp_err_t BleTransportServiceImpl::init() {
    // Subscribe to sensor data for BLE GATT notifications
    if (input.SensorDataEvents) {
        input.SensorDataEvents->Subscribe([](const Sensor::SensorData& data) {
            auto& srv = BleGattServer::Instance();
            int16_t tempCenti = static_cast<int16_t>(data.Temperature * 100.0f);
            srv.UpdateTemperature(tempCenti);
            uint16_t humidCenti = static_cast<uint16_t>(data.Humidity * 100.0f);
            srv.UpdateHumidity(humidCenti);
            ESP_LOGD(TAG, "BLE notify: temp=%d humid=%u", tempCenti, humidCenti);
        });
    }

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t BleTransportServiceImpl::start() {
    esp_err_t err = BleService::Instance().Start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BleService start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void BleTransportServiceImpl::stop() {
    BleService::Instance().Stop();
    ESP_LOGI(TAG, "Stopped");
}

} // namespace Ble
} // namespace Arcana
