#include "TimerServiceImpl.hpp"
#include "esp_log.h"

static const char* TAG = "TimerService";

namespace Arcana {
namespace Timer {

TimerServiceImpl::TimerServiceImpl() {
    output.FastTimer = new Observable<TimerTick>("TimerSvc FastTimer", 20, 3072);
    output.BaseTimer = new Observable<TimerTick>("TimerSvc BaseTimer");

    ESP_LOGI(TAG, "Created (output Observables allocated)");
}

TimerServiceImpl::~TimerServiceImpl() {
    stop();
    if (output.FastTimer) {
        delete output.FastTimer;
        output.FastTimer = nullptr;
    }
    if (output.BaseTimer) {
        delete output.BaseTimer;
        output.BaseTimer = nullptr;
    }
}

TimerService& TimerServiceImpl::getInstance() {
    static TimerServiceImpl sInstance;
    return sInstance;
}

esp_err_t TimerServiceImpl::init_HAL() {
    esp_timer_create_args_t args = {};
    args.callback = periodic_timer_callback;
    args.arg = this;
    args.name = "fast_timer";

    esp_err_t err = esp_timer_create(&args, &mTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HAL initialized");
    return ESP_OK;
}

esp_err_t TimerServiceImpl::init() {
    mBaseDivider = CONFIG_TIMER_BASE_INTERVAL_MS / CONFIG_TIMER_FAST_INTERVAL_MS;
    if (mBaseDivider == 0) mBaseDivider = 1;

    ESP_LOGI(TAG, "Initialized (fast=%dms, base=%dms, divider=%lu)",
             CONFIG_TIMER_FAST_INTERVAL_MS, CONFIG_TIMER_BASE_INTERVAL_MS,
             (unsigned long)mBaseDivider);
    return ESP_OK;
}

esp_err_t TimerServiceImpl::start() {
    mTickCount = 0;
    uint64_t interval_us = CONFIG_TIMER_FAST_INTERVAL_MS * 1000ULL;
    esp_err_t err = esp_timer_start_periodic(mTimer, interval_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Started (fast=%dms, base=%dms)",
             CONFIG_TIMER_FAST_INTERVAL_MS, CONFIG_TIMER_BASE_INTERVAL_MS);
    return ESP_OK;
}

void TimerServiceImpl::stop() {
    if (mTimer) {
        esp_timer_stop(mTimer);
    }
    ESP_LOGI(TAG, "Stopped");
}

void TimerServiceImpl::periodic_timer_callback(void* arg) {
    auto* self = static_cast<TimerServiceImpl*>(arg);
    TimerTick tick;
    tick.Timestamp = esp_timer_get_time();

    // Fast timer fires every tick
    self->output.FastTimer->Notify(tick);

    // Base timer fires every N ticks
    self->mTickCount++;
    if (self->mTickCount >= self->mBaseDivider) {
        self->mTickCount = 0;
        self->output.BaseTimer->Notify(tick);
    }
}

} // namespace Timer
} // namespace Arcana
