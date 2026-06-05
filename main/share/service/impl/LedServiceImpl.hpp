#pragma once

#include "LedService.hpp"

namespace Arcana {
namespace Led {

class LedServiceImpl : public LedService {
public:
    static LedService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

private:
    LedServiceImpl();
    ~LedServiceImpl() override;
    LedServiceImpl(const LedServiceImpl&) = delete;
    LedServiceImpl& operator=(const LedServiceImpl&) = delete;

    RgbLed* mLed = nullptr;
    std::atomic<bool> mRunning{false};
    size_t mColorIndex = 0;
};

} // namespace Led
} // namespace Arcana
