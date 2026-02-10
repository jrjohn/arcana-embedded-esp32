#pragma once

#include "TimerTypes.hpp"
#include "Observable.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Timer {

class TimerService {
public:
    struct Output {
        Observable<TimerTick>* BaseTimer = nullptr;
    };

    Output output;

    virtual ~TimerService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;
};

} // namespace Timer
} // namespace Arcana
