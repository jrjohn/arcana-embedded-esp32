#include "impl/IoServiceImpl.hpp"
#include "BoardConfig.hpp"
#include "TaskPriorities.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "IoService";

// Button wiring comes from the per-target BoardConfig.hpp (esp32/, esp32s3/)

// Debounce: require N consecutive same-state samples (N × 10ms = debounce time)
static const uint8_t DEBOUNCE_COUNT = 3;   // 30ms debounce
static const uint8_t POLL_MS = 10;         // 10ms polling interval

namespace Arcana::Io {

IoServiceImpl::IoServiceImpl() = default;

IoServiceImpl& IoServiceImpl::getInstance() {
    static IoServiceImpl sInstance;
    return sInstance;
}

esp_err_t IoServiceImpl::init_HAL() {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

#if BOARD_BUTTON_A_GPIO >= 0
    // Button A — active-LOW with internal pull-up
    io_conf.pin_bit_mask = (1ULL << BOARD_BUTTON_A_GPIO);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
#endif

    // Button B — active-LOW; pull-up capability is a board property
    io_conf.pin_bit_mask = (1ULL << BOARD_BUTTON_B_GPIO);
#if BOARD_BUTTON_B_PULLUP
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
#else
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
#endif
    gpio_config(&io_conf);

    return ESP_OK;
}

esp_err_t IoServiceImpl::init() {
    return ESP_OK;
}

esp_err_t IoServiceImpl::start() {
    BaseType_t ret = xTaskCreate(
        taskFunc, "io-key", 2048,
        this, TaskCfg::kPrioIoPoll, &mTaskHandle);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void IoServiceImpl::armCancel() {
    mCancelArmed = true;
    mCancelRequested = false;
}

void IoServiceImpl::disarmCancel() {
    mCancelArmed = false;
    mCancelRequested = false;
    mUploadRequested = false;
    mCooldownUntil = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
}

void IoServiceImpl::taskFunc(void* param) {
    auto* self = static_cast<IoServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(2000));  // let boot settle
    self->taskLoop();
    vTaskDelete(nullptr);
}

// Debounced button state tracker
struct ButtonState {
    bool stableState = true;   // true = released (active-LOW: HIGH = released)
    bool lastReading = true;
    uint8_t sameCount = 0;     // consecutive same readings

    /// Feed a new GPIO reading. Returns true if state changed.
    bool update(bool reading) {
        if (reading == lastReading) {
            if (sameCount < 255) sameCount++;
        } else {
            sameCount = 1;
            lastReading = reading;
        }

        if (sameCount >= DEBOUNCE_COUNT && lastReading != stableState) {
            stableState = lastReading;
            return true;  // state changed
        }
        return false;
    }

    bool isPressed() const { return !stableState; }  // active-LOW
};

void IoServiceImpl::taskLoop() {
#if BOARD_BUTTON_A_GPIO >= 0
    ButtonState btnA;
#endif
    ButtonState btnB;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

#if BOARD_BUTTON_A_GPIO >= 0
        // --- Button A (active-LOW): debounced rising edge = release after press ---
        bool aReading = (gpio_get_level((gpio_num_t)BOARD_BUTTON_A_GPIO) != 0);
        bool aChanged = btnA.update(aReading);

        if (aChanged && !btnA.isPressed()) {
            // Rising edge: button released (was pressed, now released)
            if (xTaskGetTickCount() >= mCooldownUntil) {
                if (mCancelArmed) {
                    mCancelRequested = true;
                    ESP_LOGI(TAG, "Button A: cancel");
                } else if (!mUploadRequested) {
                    mUploadRequested = true;
                    ESP_LOGI(TAG, "Button A: upload");
                }
            }
        }
#endif

        // --- Button B (active-LOW): debounced hold 2s = format ---
        bool bReading = (gpio_get_level((gpio_num_t)BOARD_BUTTON_B_GPIO) != 0);
        btnB.update(bReading);

        if (btnB.isPressed()) {
            mBtnBHold++;
            if (mBtnBHold >= (2000 / POLL_MS)) {  // 2 seconds
                mFormatRequested = true;
                mBtnBHold = 0;
                ESP_LOGW(TAG, "Button B: format requested (held 2s)");
            }
        } else {
            mBtnBHold = 0;
        }
    }
}

} // namespace Arcana::Io
