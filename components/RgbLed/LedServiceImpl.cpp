#include "LedServiceImpl.hpp"
#include "esp_log.h"

static const char* TAG = "LedService";

// Color table for cycling
static constexpr Arcana::Led::Rgb kColors[] = {
    {255,   0,   0},   // Red
    {  0, 255,   0},   // Green
    {  0,   0, 255},   // Blue
    {255, 255,   0},   // Yellow
    {  0, 255, 255},   // Cyan
    {255,   0, 255},   // Magenta
    {255, 128,   0},   // Orange
    {128,   0, 255},   // Purple
    {255, 255, 255},   // White
};
static constexpr size_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);

namespace Arcana {
namespace Led {

LedServiceImpl::LedServiceImpl() {
    output.LedObservable = new Observable<LedFrame>("LedSvc Observable");

    ESP_LOGI(TAG, "Created (output Observable allocated)");
}

LedServiceImpl::~LedServiceImpl() {
    stop();
    if (output.LedObservable) {
        delete output.LedObservable;
        output.LedObservable = nullptr;
    }
}

LedService& LedServiceImpl::getInstance() {
    static LedServiceImpl sInstance;
    return sInstance;
}

esp_err_t LedServiceImpl::init_HAL() {
    static RgbLed led(
        static_cast<gpio_num_t>(CONFIG_RGB_LED_GPIO),
        CONFIG_RGB_LED_NUM_LEDS
    );
    mLed = &led;

    esp_err_t err = mLed->Init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RgbLed init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HAL initialized");
    return ESP_OK;
}

esp_err_t LedServiceImpl::init() {
    // Subscribe to timer ticks for periodic color cycling
    input.TimerEvents->Subscribe([this](const Timer::TimerTick&) {
        if (!mRunning.load()) return;

        uint16_t numLeds = CONFIG_RGB_LED_NUM_LEDS;
        LedFrame frame;
        frame.Count = numLeds;
        for (uint16_t i = 0; i < numLeds; i++) {
            frame.Colors[i] = kColors[(mColorIndex + i) % kColorCount];
        }
        output.LedObservable->Notify(frame);

        ESP_LOGI(TAG, "LED[0]: R=%d G=%d B=%d",
                 frame.Colors[0].R, frame.Colors[0].G, frame.Colors[0].B);

        mColorIndex++;
    });

    // Subscribe to our own output Observable to apply frames to hardware
    output.LedObservable->Subscribe([this](const LedFrame& frame) {
        if (mLed) {
            for (uint16_t i = 0; i < frame.Count; i++) {
                mLed->SetColor(frame.Colors[i], i);
            }
            mLed->Show();
        }
    });

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t LedServiceImpl::start() {
    if (!mLed) return ESP_ERR_INVALID_STATE;

    mRunning.store(true);

    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void LedServiceImpl::stop() {
    mRunning.store(false);

    // Turn off LEDs
    uint16_t numLeds = CONFIG_RGB_LED_NUM_LEDS;
    LedFrame off;
    off.Count = numLeds;
    output.LedObservable->Notify(off);

    ESP_LOGI(TAG, "Stopped");
}

} // namespace Led
} // namespace Arcana
