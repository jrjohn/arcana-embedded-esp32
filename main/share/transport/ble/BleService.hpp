#pragma once

#include "BleTypes.hpp"
#include "BleGap.hpp"
#include "BleGattServer.hpp"
#include "BleGattClient.hpp"

#include "esp_err.h"

namespace Arcana {
namespace Ble {

class BleService {
public:
    static BleService& Instance();

    esp_err_t Init();
    esp_err_t Start();
    esp_err_t Stop();

    BleGap&        Gap()    { return BleGap::Instance(); }
    BleGattServer& Server() { return BleGattServer::Instance(); }
    BleGattClient& Client() { return BleGattClient::Instance(); }

private:
    BleService() = default;
    ~BleService() = default;
    BleService(const BleService&) = delete;
    BleService& operator=(const BleService&) = delete;

    bool mInitialized = false;
};

} // namespace Ble
} // namespace Arcana
