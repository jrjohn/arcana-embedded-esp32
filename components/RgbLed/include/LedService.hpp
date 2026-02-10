#pragma once

#include "RgbLed.hpp"
#include "Observable.hpp"
#include "TimerTypes.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Led {

static constexpr uint16_t kMaxLeds = 8;

struct LedFrame {
    Rgb Colors[kMaxLeds];
    uint16_t Count = 0;
};

class LedService {
public:
    struct Input {
        Observable<Timer::TimerTick>* TimerEvents = nullptr;
    };

    struct Output {
        Observable<LedFrame>* LedObservable = nullptr;
    };

    Input input;
    Output output;

    virtual ~LedService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;
};

} // namespace Led
} // namespace Arcana
