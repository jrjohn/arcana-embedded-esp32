#include "St7789Lcd.hpp"
#if CONFIG_IDF_TARGET_ESP32S3

#include "Xl9555.hpp"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "St7789";

// DNESP32S3 SPILCD socket wiring (see docs/DNESP32S3-pinmap.md)
static constexpr gpio_num_t kPinCs = GPIO_NUM_21;  // SLCD_CS
static constexpr gpio_num_t kPinDc = GPIO_NUM_40;  // LCD_DC via P5 (IO_SEL<->LCD_DC)
static constexpr int kPclkHz = 26 * 1000 * 1000;   // conservative — bus shared with SD

// ST7789 commands
static constexpr uint8_t kCmdSlpOut  = 0x11;
static constexpr uint8_t kCmdInvOn   = 0x21;
static constexpr uint8_t kCmdDispOn  = 0x29;
static constexpr uint8_t kCmdCaSet   = 0x2A;
static constexpr uint8_t kCmdRaSet   = 0x2B;
static constexpr uint8_t kCmdRamWr   = 0x2C;
static constexpr uint8_t kCmdMadCtl  = 0x36;
static constexpr uint8_t kCmdColMod  = 0x3A;

namespace Arcana {
namespace Lcd {

St7789Lcd::St7789Lcd()
    : Ssd1306(GPIO_NUM_NC, GPIO_NUM_NC, 0) {}  // base I2C path unused

// Drive SLCD_PWR high and pulse SLCD_RST via the board's XL9555 driver
// (DriverService brings it up before LcdService in initHAL).
esp_err_t St7789Lcd::powerAndResetViaXl9555() {
    auto& xl = Io::Xl9555::getInstance();

    esp_err_t err = xl.pinMode(Io::Xl9555::kSlcdPwr | Io::Xl9555::kSlcdRst, false);
    if (err == ESP_OK) err = xl.pinWrite(Io::Xl9555::kSlcdPwr, true);   // power on
    if (err == ESP_OK) err = xl.pinWrite(Io::Xl9555::kSlcdRst, false);  // reset low
    vTaskDelay(pdMS_TO_TICKS(20));
    if (err == ESP_OK) err = xl.pinWrite(Io::Xl9555::kSlcdRst, true);   // release
    vTaskDelay(pdMS_TO_TICKS(120));
    return err;
}

esp_err_t St7789Lcd::sendCmd(uint8_t cmd, const uint8_t* params, size_t len) {
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    gpio_set_level(kPinDc, 0);  // command
    esp_err_t err = spi_device_polling_transmit(mDev, &t);
    if (err != ESP_OK || !params || len == 0) return err;

    spi_transaction_t d = {};
    d.length = len * 8;
    d.tx_buffer = params;
    gpio_set_level(kPinDc, 1);  // data
    return spi_device_polling_transmit(mDev, &d);
}

esp_err_t St7789Lcd::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    const uint8_t ca[4] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
    const uint8_t ra[4] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
    esp_err_t err = sendCmd(kCmdCaSet, ca, 4);
    if (err == ESP_OK) err = sendCmd(kCmdRaSet, ra, 4);
    if (err == ESP_OK) err = sendCmd(kCmdRamWr);
    return err;
}

esp_err_t St7789Lcd::writePixels(const uint16_t* px, size_t count) {
    spi_transaction_t t = {};
    t.length = count * 16;
    t.tx_buffer = px;
    gpio_set_level(kPinDc, 1);  // data
    return spi_device_polling_transmit(mDev, &t);
}

esp_err_t St7789Lcd::Init() {
    // SPI2 bus is shared with the SD card; whoever runs first initializes it.
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = CONFIG_ATS_SD_MOSI_GPIO;
    bus_cfg.miso_io_num = CONFIG_ATS_SD_MISO_GPIO;
    bus_cfg.sclk_io_num = CONFIG_ATS_SD_CLK_GPIO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = powerAndResetViaXl9555();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XL9555 power/reset failed: %s", esp_err_to_name(err));
        return err;
    }

    // DC line
    gpio_config_t dc_cfg = {};
    dc_cfg.pin_bit_mask = (1ULL << kPinDc);
    dc_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&dc_cfg);
    gpio_set_level(kPinDc, 1);

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = kPclkHz;
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = kPinCs;
    dev_cfg.queue_size = 1;  // polling only

    err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &mDev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add SPI device failed: %s", esp_err_to_name(err));
        return err;
    }

    // ST7789 init — mirrors the vendor 2.4" (320x240 landscape) settings:
    // MADCTL MV|MX (swap_xy + mirror_x), RGB order, 16bpp, inversion ON.
    sendCmd(kCmdSlpOut);
    vTaskDelay(pdMS_TO_TICKS(120));
    {
        const uint8_t colmod = 0x55;  // RGB565
        sendCmd(kCmdColMod, &colmod, 1);
        const uint8_t madctl = 0x60;  // MV | MX, RGB
        sendCmd(kCmdMadCtl, &madctl, 1);
    }
    sendCmd(kCmdInvOn);

    fillScreen(0x0000);
    sendCmd(kCmdDispOn);

    mReady = true;
    Clear();
    Display();

    ESP_LOGI(TAG, "Initialized (ST7789 320x240, SPI2 polling @%dMHz, CS=%d DC=%d)",
             kPclkHz / 1000000, kPinCs, kPinDc);
    return ESP_OK;
}

void St7789Lcd::fillScreen(uint16_t color565) {
    static uint16_t buf[kPanelW * 4];  // 4 rows per chunk (DMA-capable .bss)
    for (size_t i = 0; i < kPanelW * 4; i++) buf[i] = color565;
    setWindow(0, 0, kPanelW - 1, kPanelH - 1);
    for (int y = 0; y < kPanelH; y += 4) {
        writePixels(buf, kPanelW * 4);
    }
}

void St7789Lcd::Display() {
    if (!mReady) return;

    // 128x64 mono framebuffer -> 2x upscale (256x128), centered on 320x240
    static constexpr int kX0 = (kPanelW - kWidth * 2) / 2;   // 32
    static constexpr int kY0 = (kPanelH - kHeight * 2) / 2;  // 56
    static uint16_t buf[kWidth * 2 * 2];  // two output rows of 256 px

    setWindow(kX0, kY0, kX0 + kWidth * 2 - 1, kY0 + kHeight * 2 - 1);
    for (int row = 0; row < kHeight; row++) {
        for (int col = 0; col < kWidth; col++) {
            // SSD1306 page layout: byte = 8 vertical pixels. White text —
            // 0x0000/0xFFFF are endian-invariant so no byte swap needed.
            bool on = (mBuffer[(row / 8) * kWidth + col] >> (row % 8)) & 0x01;
            uint16_t c = on ? 0xFFFF : 0x0000;
            buf[col * 2]     = c;
            buf[col * 2 + 1] = c;
        }
        memcpy(buf + kWidth * 2, buf, kWidth * 2 * sizeof(uint16_t));
        writePixels(buf, kWidth * 2 * 2);  // polling — buffer reusable on return
    }
}

} // namespace Lcd
} // namespace Arcana

#endif // CONFIG_IDF_TARGET_ESP32S3
