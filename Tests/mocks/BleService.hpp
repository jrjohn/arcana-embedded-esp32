#pragma once
// Stub BleService.hpp for unit tests.
// Provides only the singleton APIs that command headers reference:
//   - Arcana::Ble::BleGattServer::Instance().GetConnectionCount()
//   - Arcana::Ble::BleGap::Instance().StartScanning(durationSec)
//
// Real BLE stack pulls in esp_gap_ble_api.h / esp_gatts_api.h which require
// the ESP-IDF Bluedroid headers. Tests don't actually exercise BLE — the
// commands that use these are tested via factory creation only.

#include "esp_err.h"
#include <cstdint>

namespace Arcana {
namespace Ble {

class BleGattServer {
public:
    static BleGattServer& Instance() {
        static BleGattServer s;
        return s;
    }
    uint8_t GetConnectionCount() const { return 0; }
private:
    BleGattServer() = default;
};

class BleGap {
public:
    static BleGap& Instance() {
        static BleGap g;
        return g;
    }
    esp_err_t StartScanning(uint32_t /*durationSec*/) {
        return mScanResult;
    }

    // Test injection
    void test_setScanResult(esp_err_t r) { mScanResult = r; }

private:
    BleGap() = default;
    esp_err_t mScanResult = ESP_OK;
};

} // namespace Ble
} // namespace Arcana
