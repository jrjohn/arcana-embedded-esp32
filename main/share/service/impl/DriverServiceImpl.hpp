#pragma once

#include "DriverService.hpp"

namespace Arcana {
namespace Driver {

class DriverServiceImpl : public DriverService {
public:
    static DriverService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    bool hasExpander() const override;
    esp_err_t expanderPinMode(uint16_t mask, bool input) override;
    esp_err_t expanderPinWrite(uint16_t mask, bool level) override;
    esp_err_t expanderRead(uint16_t& value) override;

private:
    DriverServiceImpl() = default;
};

} // namespace Driver
} // namespace Arcana
