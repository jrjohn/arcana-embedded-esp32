/*
 * WS2812B RGB LED Driver for ESP32
 *
 * Uses RMT peripheral for precise single-wire timing.
 * Supports color cycling via FreeRTOS task.
 */

#pragma once

#include <cstdint>
#include <atomic>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

namespace Arcana {
namespace Led {

struct Rgb {
    uint8_t R, G, B;

    constexpr Rgb() : R(0), G(0), B(0) {}
    constexpr Rgb(uint8_t r, uint8_t g, uint8_t b) : R(r), G(g), B(b) {}
};

/**
 * @brief WS2812B RGB LED controller using RMT peripheral
 *
 * Usage:
 * @code
 *   RgbLed led(GPIO_NUM_26);
 *   led.Init();
 *   led.SetColor({255, 0, 0});   // Red
 *   led.StartColorCycle(1000);    // Cycle every 1s
 * @endcode
 */
class RgbLed {
public:
    explicit RgbLed(gpio_num_t GpioPin, uint16_t NumLeds = 1);
    ~RgbLed();

    RgbLed(const RgbLed&) = delete;
    RgbLed& operator=(const RgbLed&) = delete;

    esp_err_t Init();

    esp_err_t SetColor(const Rgb& Color, uint16_t Index = 0);
    esp_err_t SetAll(const Rgb& Color);
    esp_err_t Show();

    esp_err_t StartColorCycle(uint32_t IntervalMs = 1000);
    esp_err_t StopColorCycle();
    bool IsCycling() const { return mCycling.load(); }

private:
    static void CycleTask(void* Arg);
    void CycleLoop();

    gpio_num_t mGpioPin;
    uint16_t mNumLeds;

    rmt_channel_handle_t mChannel;
    rmt_encoder_handle_t mEncoder;

    uint8_t* mPixelBuf;      // GRB data, 3 bytes per LED
    bool mInitialized;

    TaskHandle_t mTaskHandle;
    std::atomic<bool> mCycling;
    std::atomic<bool> mShouldStop;
    uint32_t mCycleIntervalMs;
};

} // namespace Led
} // namespace Arcana
