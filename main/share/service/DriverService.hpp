#pragma once

#include "esp_err.h"
#include <cstdint>

namespace Arcana {
namespace Driver {

/**
 * Driver Service — board-level shared hardware lifecycle.
 * Owns resources that belong to the BOARD rather than to one feature
 * service: the XL9555 IO expander on the DNESP32S3 (keys, camera
 * PWDN/RESET, LCD power/reset, beeper, speaker enable, INT release).
 * Must init_HAL() before any service whose hardware hangs off these
 * resources (LCD, storage, camera, audio).
 *
 * Boards without an expander (classic ESP32 DevKit) report
 * hasExpander() == false and the pin ops return ESP_ERR_NOT_SUPPORTED.
 * Pin masks live in driver/Xl9555.hpp (target-gated).
 */
class DriverService {
public:
    struct Input {};   // No inputs — hardware only
    struct Output {};

    Input input;
    Output output;

    virtual ~DriverService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    /// Board IO expander access (XL9555 on DNESP32S3)
    virtual bool hasExpander() const = 0;
    virtual esp_err_t expanderPinMode(uint16_t mask, bool input) = 0;
    virtual esp_err_t expanderPinWrite(uint16_t mask, bool level) = 0;
    virtual esp_err_t expanderRead(uint16_t& value) = 0;
};

} // namespace Driver
} // namespace Arcana
