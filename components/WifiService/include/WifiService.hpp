#pragma once

#include "esp_err.h"
#include <cstdint>

namespace Arcana::Wifi {

class WifiService {
public:
    virtual ~WifiService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    /// Sync time via SNTP. Blocks up to timeoutMs.
    virtual esp_err_t syncNtp(uint32_t timeoutMs = 10000) = 0;

protected:
    WifiService() = default;
};

} // namespace Arcana::Wifi
