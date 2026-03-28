/*
 * WS2812B RGB LED Driver Implementation
 *
 * Uses a custom RMT encoder (bytes_encoder + copy_encoder) that appends
 * a reset code (>280µs LOW) after pixel data, following the ESP-IDF
 * led_strip example pattern.
 *
 * Data order: GRB, MSB first
 * Bit 0: HIGH 0.3µs + LOW 0.9µs
 * Bit 1: HIGH 0.9µs + LOW 0.3µs
 * Reset: LOW ≥280µs
 */

#include "RgbLed.hpp"
#include "esp_log.h"
#include "esp_check.h"
#include <cstring>

static const char* TAG = "RgbLed";

// WS2812B timing at 10MHz resolution (1 tick = 0.1µs)
static constexpr uint16_t kT0H = 3;   // 0.3µs
static constexpr uint16_t kT0L = 9;   // 0.9µs
static constexpr uint16_t kT1H = 9;   // 0.9µs
static constexpr uint16_t kT1L = 3;   // 0.3µs
static constexpr uint16_t kResetTicks = 2800;  // 280µs reset

// Use shared color table from RgbLed.hpp (Arcana::Led::kCycleColors)

/*******************************************************************************
 * Custom WS2812 RMT Encoder (bytes data + reset code)
 *
 * State machine:
 *   State 0 → bytes_encoder encodes GRB pixel data
 *   State 1 → copy_encoder emits reset symbol (LOW ≥280µs)
 *   Done    → reset state for next call
 ******************************************************************************/

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t* encoder,
                            rmt_channel_handle_t channel,
                            const void* primary_data, size_t data_size,
                            rmt_encode_state_t* ret_state)
{
    auto* ws = reinterpret_cast<ws2812_encoder_t*>(encoder);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (ws->state) {
    case 0: {
        // Encode pixel data (GRB bytes → RMT symbols)
        encoded_symbols += ws->bytes_encoder->encode(
            ws->bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            ws->state = 1;  // Move to reset code
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
            return encoded_symbols;
        }
    }
    [[fallthrough]];
    case 1: {
        // Emit reset code (one symbol: LOW for ≥280µs)
        encoded_symbols += ws->copy_encoder->encode(
            ws->copy_encoder, channel, &ws->reset_code,
            sizeof(ws->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            ws->state = 0;  // Reset for next frame
            *ret_state = RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
        }
        return encoded_symbols;
    }
    }

    *ret_state = RMT_ENCODING_RESET;
    return encoded_symbols;
}

static esp_err_t ws2812_reset(rmt_encoder_t* encoder)
{
    auto* ws = reinterpret_cast<ws2812_encoder_t*>(encoder);
    rmt_encoder_reset(ws->bytes_encoder);
    rmt_encoder_reset(ws->copy_encoder);
    ws->state = 0;
    return ESP_OK;
}

static esp_err_t ws2812_del(rmt_encoder_t* encoder)
{
    auto* ws = reinterpret_cast<ws2812_encoder_t*>(encoder);
    rmt_del_encoder(ws->bytes_encoder);
    rmt_del_encoder(ws->copy_encoder);
    delete ws;
    return ESP_OK;
}

static esp_err_t ws2812_encoder_new(rmt_encoder_handle_t* ret_encoder)
{
    auto* ws = new (std::nothrow) ws2812_encoder_t();
    if (!ws) return ESP_ERR_NO_MEM;

    ws->base.encode = ws2812_encode;
    ws->base.reset = ws2812_reset;
    ws->base.del = ws2812_del;
    ws->state = 0;

    // Reset symbol: LOW for ≥280µs
    ws->reset_code.duration0 = kResetTicks;
    ws->reset_code.level0 = 0;
    ws->reset_code.duration1 = 0;
    ws->reset_code.level1 = 0;

    // Bytes encoder for pixel data
    rmt_bytes_encoder_config_t bytes_cfg = {};
    bytes_cfg.bit0.duration0 = kT0H;
    bytes_cfg.bit0.level0 = 1;
    bytes_cfg.bit0.duration1 = kT0L;
    bytes_cfg.bit0.level1 = 0;
    bytes_cfg.bit1.duration0 = kT1H;
    bytes_cfg.bit1.level0 = 1;
    bytes_cfg.bit1.duration1 = kT1L;
    bytes_cfg.bit1.level1 = 0;
    bytes_cfg.flags.msb_first = 1;

    esp_err_t err = rmt_new_bytes_encoder(&bytes_cfg, &ws->bytes_encoder);
    if (err != ESP_OK) {
        delete ws;
        return err;
    }

    // Copy encoder for reset symbol
    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &ws->copy_encoder);
    if (err != ESP_OK) {
        rmt_del_encoder(ws->bytes_encoder);
        delete ws;
        return err;
    }

    *ret_encoder = &ws->base;
    return ESP_OK;
}

/*******************************************************************************
 * RgbLed Implementation
 ******************************************************************************/

namespace Arcana {
namespace Led {

RgbLed::RgbLed(gpio_num_t GpioPin, uint16_t NumLeds)
    : mGpioPin(GpioPin)
    , mNumLeds(NumLeds)
    , mChannel(nullptr)
    , mEncoder(nullptr)
    , mPixelBuf(nullptr)
    , mInitialized(false)
    , mTaskHandle(nullptr)
    , mCycling(false)
    , mShouldStop(false)
    , mCycleIntervalMs(1000)
{
}

RgbLed::~RgbLed() {
    StopColorCycle();

    if (mChannel) {
        rmt_disable(mChannel);
        rmt_del_channel(mChannel);
    }
    if (mEncoder) {
        rmt_del_encoder(mEncoder);
    }
    delete[] mPixelBuf;
}

esp_err_t RgbLed::Init() {
    if (mInitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    mPixelBuf = new (std::nothrow) uint8_t[mNumLeds * 3]();
    if (!mPixelBuf) {
        return ESP_ERR_NO_MEM;
    }

    // RMT TX channel
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = mGpioPin;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = 10000000;  // 10MHz
    tx_cfg.mem_block_symbols = 64;
    tx_cfg.trans_queue_depth = 4;

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &mChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    // Custom WS2812 encoder (bytes + reset)
    err = ws2812_encoder_new(&mEncoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ws2812_encoder_new failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(mChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    mInitialized = true;
    ESP_LOGI(TAG, "WS2812B initialized on GPIO%d (%d LEDs)", mGpioPin, mNumLeds);
    return ESP_OK;
}

/*******************************************************************************
 * Color Control
 ******************************************************************************/

esp_err_t RgbLed::SetColor(const Rgb& Color, uint16_t Index) {
    if (!mInitialized || Index >= mNumLeds) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = Index * 3;
    mPixelBuf[offset + 0] = Color.G;
    mPixelBuf[offset + 1] = Color.R;
    mPixelBuf[offset + 2] = Color.B;

    return ESP_OK;
}

esp_err_t RgbLed::SetAll(const Rgb& Color) {
    for (uint16_t i = 0; i < mNumLeds; i++) {
        SetColor(Color, i);
    }
    return ESP_OK;
}

esp_err_t RgbLed::Show() {
    if (!mInitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;

    esp_err_t err = rmt_transmit(mChannel, mEncoder,
                                  mPixelBuf, mNumLeds * 3, &tx_cfg);
    if (err != ESP_OK) {
        return err;
    }

    return rmt_tx_wait_all_done(mChannel, pdMS_TO_TICKS(100));
}

/*******************************************************************************
 * Color Cycle Task
 ******************************************************************************/

esp_err_t RgbLed::StartColorCycle(uint32_t IntervalMs) {
    if (!mInitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mCycling.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    mCycleIntervalMs = IntervalMs;
    mShouldStop.store(false);
    mCycling.store(true);

    BaseType_t ret = xTaskCreate(CycleTask, "rgb_cycle", 2048, this, 3, &mTaskHandle);
    if (ret != pdPASS) {
        mCycling.store(false);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Color cycle started (interval=%ums)", IntervalMs);
    return ESP_OK;
}

esp_err_t RgbLed::StopColorCycle() {
    if (!mCycling.load()) {
        return ESP_OK;
    }

    mShouldStop.store(true);
    int timeout = 20;
    while (mCycling.load() && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }

    mTaskHandle = nullptr;
    ESP_LOGI(TAG, "Color cycle stopped");
    return ESP_OK;
}

void RgbLed::CycleTask(void* Arg) {
    auto* self = static_cast<RgbLed*>(Arg);
    self->CycleLoop();
    vTaskDelete(nullptr);
}

void RgbLed::CycleLoop() {
    size_t idx = 0;

    while (!mShouldStop.load()) {
        for (uint16_t i = 0; i < mNumLeds; i++) {
            const Rgb& c = kCycleColors[(idx + i) % kCycleColorCount];
            SetColor(c, i);
        }
        Show();

        const Rgb& c0 = kCycleColors[idx % kCycleColorCount];
        ESP_LOGI(TAG, "LED[0]: R=%d G=%d B=%d", c0.R, c0.G, c0.B);

        idx++;
        vTaskDelay(pdMS_TO_TICKS(mCycleIntervalMs));
    }

    SetAll({0, 0, 0});
    Show();

    mCycling.store(false);
}

} // namespace Led
} // namespace Arcana
