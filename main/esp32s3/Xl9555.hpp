#pragma once

#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <cstdint>

namespace Arcana {
namespace Io {

/**
 * XL9555 16-bit I2C IO expander — DNESP32S3 board controller.
 *
 * Owns a persistent I2C0 bus (IO42/IO41, addr 0x20) and shadows the
 * output/config registers so pin writes are single I2C transactions and
 * read-modify-write is race-free (mutex-guarded). init() also reads both
 * input ports, which releases the expander's latched open-drain INT line
 * (a stuck INT can hold SPI_MISO low through the P5 jumper and corrupt
 * every SD transaction — found during board bring-up).
 *
 * Pin masks follow the vendor BSP naming (xl9555.h, A-disk 10_spilcd).
 */
class Xl9555 {
public:
    // Port 0 (bits 0-7) / Port 1 (bits 8-15)
    static constexpr uint16_t kApInt    = 0x0001;
    static constexpr uint16_t kQmaInt   = 0x0002;
    static constexpr uint16_t kSpkEn    = 0x0004;
    static constexpr uint16_t kBeep     = 0x0008;
    static constexpr uint16_t kOvPwdn   = 0x0010;
    static constexpr uint16_t kOvReset  = 0x0020;
    static constexpr uint16_t kGbcLed   = 0x0040;
    static constexpr uint16_t kGbcKey   = 0x0080;
    static constexpr uint16_t kLcdBl    = 0x0100;
    static constexpr uint16_t kCtRst    = 0x0200;
    static constexpr uint16_t kSlcdRst  = 0x0400;
    static constexpr uint16_t kSlcdPwr  = 0x0800;
    static constexpr uint16_t kKey3     = 0x1000;
    static constexpr uint16_t kKey2     = 0x2000;
    static constexpr uint16_t kKey1     = 0x4000;
    static constexpr uint16_t kKey0     = 0x8000;

    static Xl9555& getInstance();

    /// Bring up the persistent I2C bus, read inputs (releases INT).
    esp_err_t init();
    bool isReady() const { return mReady; }

    /// Configure pins in `mask` as outputs (false) or inputs (true=default).
    esp_err_t pinMode(uint16_t mask, bool input);
    /// Drive output pins in `mask` high/low (pins must be outputs).
    esp_err_t pinWrite(uint16_t mask, bool level);
    /// Read both input ports (also re-arms INT).
    esp_err_t readInputs(uint16_t& value);

private:
    Xl9555() = default;

    esp_err_t writeReg(uint8_t reg, uint8_t val);
    esp_err_t readReg(uint8_t reg, uint8_t& val);

    i2c_master_bus_handle_t mBus = nullptr;
    i2c_master_dev_handle_t mDev = nullptr;
    SemaphoreHandle_t mMutex = nullptr;
    uint16_t mOutShadow = 0xFFFF;   // power-on default: all high
    uint16_t mCfgShadow = 0xFFFF;   // power-on default: all inputs
    bool mReady = false;
};

} // namespace Io
} // namespace Arcana

