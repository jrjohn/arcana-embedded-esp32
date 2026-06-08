#include "impl/DriverServiceImpl.hpp"
#include "esp_log.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "Xl9555.hpp"
#endif

static const char* TAG = "DriverService";

namespace Arcana {
namespace Driver {

DriverService& DriverServiceImpl::getInstance() {
    static DriverServiceImpl sInstance;
    return sInstance;
}

esp_err_t DriverServiceImpl::init_HAL() {
#if CONFIG_IDF_TARGET_ESP32S3
    // Brings up the persistent I2C0 bus and reads the input ports, which
    // releases the expander's latched INT (it can hold SPI_MISO low via
    // the P5 jumper and corrupt every SD transaction).
    esp_err_t err = Io::Xl9555::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XL9555 init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "HAL initialized (XL9555 expander)");
#else
    ESP_LOGI(TAG, "HAL initialized (no board expander)");
#endif
    return ESP_OK;
}

esp_err_t DriverServiceImpl::init() {
    return ESP_OK;  // hardware only — nothing to wire
}

esp_err_t DriverServiceImpl::start() {
    return ESP_OK;  // no task
}

void DriverServiceImpl::stop() {}

bool DriverServiceImpl::hasExpander() const {
#if CONFIG_IDF_TARGET_ESP32S3
    return Io::Xl9555::getInstance().isReady();
#else
    return false;
#endif
}

esp_err_t DriverServiceImpl::expanderPinMode(uint16_t mask, bool input) {
#if CONFIG_IDF_TARGET_ESP32S3
    return Io::Xl9555::getInstance().pinMode(mask, input);
#else
    (void)mask; (void)input;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t DriverServiceImpl::expanderPinWrite(uint16_t mask, bool level) {
#if CONFIG_IDF_TARGET_ESP32S3
    return Io::Xl9555::getInstance().pinWrite(mask, level);
#else
    (void)mask; (void)level;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t DriverServiceImpl::expanderRead(uint16_t& value) {
#if CONFIG_IDF_TARGET_ESP32S3
    return Io::Xl9555::getInstance().readInputs(value);
#else
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace Driver
} // namespace Arcana
