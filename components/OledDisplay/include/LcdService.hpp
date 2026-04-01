#pragma once

#include "Ssd1306.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Lcd {

/**
 * LCD Service — hardware lifecycle only.
 * Does NOT subscribe to any Observable (MVVM: ViewModel handles that).
 * Exposes display reference for View injection.
 */
class LcdService {
public:
    struct Input {};   // No inputs — hardware only
    struct Output {};

    Input input;
    Output output;

    virtual ~LcdService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    /// Access display hardware (for Controller to inject into View)
    virtual Ssd1306& getDisplay() = 0;
};

} // namespace Lcd
} // namespace Arcana
