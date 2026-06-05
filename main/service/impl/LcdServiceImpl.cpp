#include "impl/LcdServiceImpl.hpp"
#include "St7789Lcd.hpp"
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
#if CONFIG_IDF_TARGET_ESP32S3
    // DNESP32S3: ST7789 SPI LCD module (2.4"/1.3") instead of an SSD1306
    static St7789Lcd display;
#else
    static Ssd1306 display(
        static_cast<gpio_num_t>(CONFIG_OLED_SCL_GPIO),
        static_cast<gpio_num_t>(CONFIG_OLED_SDA_GPIO),
        CONFIG_OLED_I2C_ADDR
    );
#endif
    mOled = &display;

    esp_err_t err = mOled->Init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));
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
