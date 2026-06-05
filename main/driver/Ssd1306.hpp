#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <cstdint>

namespace Arcana {
namespace Lcd {

class Ssd1306 {
public:
    static constexpr uint16_t kWidth  = 128;
    static constexpr uint16_t kHeight = 64;
    static constexpr uint16_t kPages  = kHeight / 8;
    static constexpr size_t   kBufSize = kWidth * kPages;  // 1024 bytes

    Ssd1306(gpio_num_t sclPin, gpio_num_t sdaPin, uint8_t addr = 0x3C);
    ~Ssd1306();

    esp_err_t Init();
    void Clear();
    void SetCursor(uint8_t col, uint8_t page);
    void DrawChar(char c);
    void DrawString(const char* str);
    void DrawStringAt(uint8_t col, uint8_t page, const char* str);
    void Display();

private:
    esp_err_t SendCommand(uint8_t cmd);
    esp_err_t SendCommands(const uint8_t* cmds, size_t len);

    gpio_num_t mSclPin;
    gpio_num_t mSdaPin;
    uint8_t mAddr;
    i2c_master_bus_handle_t mBusHandle = nullptr;
    i2c_master_dev_handle_t mDevHandle = nullptr;

    uint8_t mBuffer[kBufSize] = {};
    uint8_t mCursorCol  = 0;
    uint8_t mCursorPage = 0;
    bool mReady = false;  // set after a successful Init(); gates I2C traffic
};

} // namespace Lcd
} // namespace Arcana
