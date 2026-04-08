#pragma once

#include "TimerTypes.hpp"
#include "Observable.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Diagnostic {

class DiagnosticService {
public:
    struct Input {
        Observable<Timer::TimerTick>* TimerEvents = nullptr;
    };

    Input input;

    virtual ~DiagnosticService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;
};

} // namespace Diagnostic
} // namespace Arcana
