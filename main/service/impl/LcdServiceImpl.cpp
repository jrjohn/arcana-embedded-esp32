#include "impl/LcdServiceImpl.hpp"
#include "esp_log.h"

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
    // Hardware-only service — no subscriptions
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t LcdServiceImpl::start() {
    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void LcdServiceImpl::stop() {
    if (mOled) {
        mOled->Clear();
        mOled->Display();
    }
    ESP_LOGI(TAG, "Stopped");
}

} // namespace Lcd
} // namespace Arcana
