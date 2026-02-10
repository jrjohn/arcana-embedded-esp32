#pragma once

#include "LcdService.hpp"
#include "Ssd1306.hpp"
#include <atomic>

namespace Arcana {
namespace Lcd {

class LcdServiceImpl : public LcdService {
public:
    static LcdService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

private:
    LcdServiceImpl();
    ~LcdServiceImpl() override = default;
    LcdServiceImpl(const LcdServiceImpl&) = delete;
    LcdServiceImpl& operator=(const LcdServiceImpl&) = delete;

    Ssd1306* mOled = nullptr;
    std::atomic<bool> mRunning{false};
};

} // namespace Lcd
} // namespace Arcana
