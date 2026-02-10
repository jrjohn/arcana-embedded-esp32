#pragma once

#include "BleTransportService.hpp"
#include "BleService.hpp"

namespace Arcana {
namespace Ble {

class BleTransportServiceImpl : public BleTransportService {
public:
    static BleTransportService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    BleGattServer& server() override { return BleGattServer::Instance(); }

private:
    BleTransportServiceImpl() = default;
    ~BleTransportServiceImpl() override = default;
    BleTransportServiceImpl(const BleTransportServiceImpl&) = delete;
    BleTransportServiceImpl& operator=(const BleTransportServiceImpl&) = delete;
};

} // namespace Ble
} // namespace Arcana
