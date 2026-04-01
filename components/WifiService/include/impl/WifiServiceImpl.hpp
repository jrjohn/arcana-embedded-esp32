#pragma once

#include "WifiService.hpp"
#include <atomic>

namespace Arcana::Wifi {

class WifiServiceImpl : public WifiService {
public:
    static WifiServiceImpl& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t connect() override;
    void disconnect() override;
    bool isConnected() const override { return mConnected.load(); }
    esp_err_t syncNtp(uint32_t timeoutMs = 10000) override;

private:
    WifiServiceImpl() = default;

    std::atomic<bool> mConnected{false};
};

} // namespace Arcana::Wifi
