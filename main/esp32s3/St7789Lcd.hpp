#pragma once

#include "sdkconfig.h"
#include "Ssd1306.hpp"
#include "driver/spi_master.h"

namespace Arcana {
namespace Lcd {

/**
 * ST7789 SPI LCD (ALIENTEK 2.4" / 1.3" TFTLCD module on the DNESP32S3
 * SPILCD socket). Reuses the Ssd1306 128x64 monochrome framebuffer and
 * text drawing; Display() upscales it 2x (256x128) centered on the
 * 320x240 panel.
 *
 * Deliberately drives the panel with raw spi_master POLLING transactions
 * instead of esp_lcd: the bus (SPI2) is shared with the SD card, whose
 * sdspi driver also uses polling. Mixing sdspi polling with esp_lcd's
 * interrupt-queued transactions races the spi bus lock and asserts in
 * spi_intr (cur_cs != DEV_NUM_MAX) — observed within seconds of the ATS
 * writer and the view refresh running together. All-polling devices on
 * one bus is the supported, race-free arrangement.
 *
 * Board wiring: SPI2 shared with TF (MOSI=11/SCK=12/MISO=13), CS=IO21
 * (SLCD_CS), DC=IO40 (P5 jumper IO_SEL<->LCD_DC), PWR/RST on the XL9555
 * (IO1_3 / IO1_2). Pin map + panel parameters follow the vendor
 * 10_spilcd BSP (ATK 2.4": 320x240 landscape, INVON, MADCTL MV|MX).
 */
class St7789Lcd : public Ssd1306 {
public:
    static constexpr uint16_t kPanelW = 320;
    static constexpr uint16_t kPanelH = 240;

    St7789Lcd();
    ~St7789Lcd() override = default;

    esp_err_t Init() override;
    void Display() override;

private:
    esp_err_t powerAndResetViaXl9555();
    esp_err_t sendCmd(uint8_t cmd, const uint8_t* params = nullptr, size_t len = 0);
    esp_err_t setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    esp_err_t writePixels(const uint16_t* px, size_t count);
    void fillScreen(uint16_t color565);

    spi_device_handle_t mDev = nullptr;
};

} // namespace Lcd
} // namespace Arcana

