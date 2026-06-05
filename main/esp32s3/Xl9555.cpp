#include "Xl9555.hpp"
#include "esp_log.h"

static const char* TAG = "Xl9555";

// XL9555 registers
static constexpr uint8_t kRegIn0  = 0x00;  // auto-increments to IN1
static constexpr uint8_t kRegOut0 = 0x02;  // auto-increments to OUT1
static constexpr uint8_t kRegCfg0 = 0x06;  // auto-increments to CFG1

namespace Arcana {
namespace Io {

Xl9555& Xl9555::getInstance() {
    static Xl9555 sInstance;
    return sInstance;
}

esp_err_t Xl9555::init() {
    if (mReady) return ESP_OK;

    mMutex = xSemaphoreCreateMutex();
    if (!mMutex) return ESP_ERR_NO_MEM;

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.scl_io_num = GPIO_NUM_42;
    bus_cfg.sda_io_num = GPIO_NUM_41;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;  // board has 2.2K externals

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &mBus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = 0x20;  // A0-A2 strapped to GND
    dev_cfg.scl_speed_hz = 100000;

    err = i2c_master_bus_add_device(mBus, &dev_cfg, &mDev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(mBus);
        mBus = nullptr;
        return err;
    }

    // Read both input ports — releases the latched open-drain INT line
    uint16_t in = 0;
    mReady = true;  // readInputs needs the ready path
    err = readInputs(in);
    if (err != ESP_OK) {
        mReady = false;
        ESP_LOGE(TAG, "input read failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Ready (inputs=0x%04x, INT released)", in);
    return ESP_OK;
}

esp_err_t Xl9555::writeReg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = {reg, val};
    return i2c_master_transmit(mDev, tx, 2, 100);
}

esp_err_t Xl9555::readReg(uint8_t reg, uint8_t& val) {
    return i2c_master_transmit_receive(mDev, &reg, 1, &val, 1, 100);
}

esp_err_t Xl9555::pinMode(uint16_t mask, bool input) {
    if (!mReady) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mMutex, portMAX_DELAY);

    uint16_t cfg = input ? (mCfgShadow | mask) : (mCfgShadow & ~mask);
    esp_err_t err = ESP_OK;
    if ((cfg & 0x00FF) != (mCfgShadow & 0x00FF)) {
        err = writeReg(kRegCfg0, (uint8_t)(cfg & 0xFF));
    }
    if (err == ESP_OK && (cfg & 0xFF00) != (mCfgShadow & 0xFF00)) {
        err = writeReg(kRegCfg0 + 1, (uint8_t)(cfg >> 8));
    }
    if (err == ESP_OK) mCfgShadow = cfg;

    xSemaphoreGive(mMutex);
    return err;
}

esp_err_t Xl9555::pinWrite(uint16_t mask, bool level) {
    if (!mReady) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mMutex, portMAX_DELAY);

    uint16_t out = level ? (mOutShadow | mask) : (mOutShadow & ~mask);
    esp_err_t err = ESP_OK;
    if ((out & 0x00FF) != (mOutShadow & 0x00FF)) {
        err = writeReg(kRegOut0, (uint8_t)(out & 0xFF));
    }
    if (err == ESP_OK && (out & 0xFF00) != (mOutShadow & 0xFF00)) {
        err = writeReg(kRegOut0 + 1, (uint8_t)(out >> 8));
    }
    if (err == ESP_OK) mOutShadow = out;

    xSemaphoreGive(mMutex);
    return err;
}

esp_err_t Xl9555::readInputs(uint16_t& value) {
    if (!mReady) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mMutex, portMAX_DELAY);

    uint8_t in[2] = {};
    uint8_t reg = kRegIn0;
    esp_err_t err = i2c_master_transmit_receive(mDev, &reg, 1, in, 2, 100);
    if (err == ESP_OK) value = (uint16_t)in[0] | ((uint16_t)in[1] << 8);

    xSemaphoreGive(mMutex);
    return err;
}

} // namespace Io
} // namespace Arcana

