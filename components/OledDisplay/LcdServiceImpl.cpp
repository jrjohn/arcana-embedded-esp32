#include "impl/LcdServiceImpl.hpp"
#include "esp_log.h"
#include <cstdio>

static const char* TAG = "LcdService";

namespace Arcana {
namespace Lcd {

LcdServiceImpl::LcdServiceImpl() {
    ESP_LOGI(TAG, "Created");
}

LcdService& LcdServiceImpl::getInstance() {
    static LcdServiceImpl sInstance;
    return sInstance;
}

esp_err_t LcdServiceImpl::init_HAL() {
    static Ssd1306 oled(
        static_cast<gpio_num_t>(CONFIG_OLED_SCL_GPIO),
        static_cast<gpio_num_t>(CONFIG_OLED_SDA_GPIO),
        CONFIG_OLED_I2C_ADDR
    );
    mOled = &oled;

    esp_err_t err = mOled->Init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HAL initialized");
    return ESP_OK;
}

esp_err_t LcdServiceImpl::init() {
    // Subscribe to sensor data events and update display
    input.SensorDataEvents->Subscribe([this](const Sensor::SensorData& data) {
        if (!mRunning.load() || !mOled) return;

        char line[22]; // 21 chars max for 128px / 6px per char

        mOled->Clear();

        // Title line
        mOled->DrawStringAt(0, 0, "  Arcana ESP32");

        // Separator
        mOled->DrawStringAt(0, 2, "--------------------");

        // Temperature
        snprintf(line, sizeof(line), " Temp:  %5.1f C", data.Temperature);
        mOled->DrawStringAt(0, 4, line);

        // Humidity
        snprintf(line, sizeof(line), " Humi:  %5.1f %%", data.Humidity);
        mOled->DrawStringAt(0, 6, line);

        mOled->Display();
    });

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t LcdServiceImpl::start() {
    if (!mOled) return ESP_ERR_INVALID_STATE;

    mRunning.store(true);

    // Show startup screen
    mOled->Clear();
    mOled->DrawStringAt(0, 0, "  Arcana ESP32");
    mOled->DrawStringAt(0, 2, "--------------------");
    mOled->DrawStringAt(0, 4, " Temp:   --.- C");
    mOled->DrawStringAt(0, 6, " Humi:   --.- %");
    mOled->Display();

    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void LcdServiceImpl::stop() {
    mRunning.store(false);
    if (mOled) {
        mOled->Clear();
        mOled->Display();
    }
    ESP_LOGI(TAG, "Stopped");
}

} // namespace Lcd
} // namespace Arcana
