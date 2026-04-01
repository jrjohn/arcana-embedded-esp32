#pragma once

#include "LcdService.hpp"

namespace Arcana {
namespace Lcd {

class LcdServiceImpl : public LcdService {
public:
    static LcdService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    Ssd1306& getDisplay() override { return *mOled; }

private:
    LcdServiceImpl();
    ~LcdServiceImpl() override = default;
    LcdServiceImpl(const LcdServiceImpl&) = delete;
    LcdServiceImpl& operator=(const LcdServiceImpl&) = delete;

    Ssd1306* mOled = nullptr;
};

} // namespace Lcd
} // namespace Arcana
