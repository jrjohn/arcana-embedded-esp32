#pragma once

#include "TimerService.hpp"
#include "esp_timer.h"

namespace Arcana {
namespace Timer {

class TimerServiceImpl : public TimerService {
public:
    static TimerService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

private:
    TimerServiceImpl();
    ~TimerServiceImpl() override;
    TimerServiceImpl(const TimerServiceImpl&) = delete;
    TimerServiceImpl& operator=(const TimerServiceImpl&) = delete;

    static void periodic_timer_callback(void* arg);

    esp_timer_handle_t mTimer = nullptr;
};

} // namespace Timer
} // namespace Arcana
