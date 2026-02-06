#pragma once

#include "BleTypes.hpp"
#include "Observable.hpp"

#include "esp_gap_ble_api.h"

namespace Arcana {
namespace Ble {

class BleGap {
public:
    static BleGap& Instance();

    esp_err_t Init(const char* deviceName);
    esp_err_t StartAdvertising();
    esp_err_t StopAdvertising();
    esp_err_t StartScanning(uint32_t durationSec);
    esp_err_t StopScanning();

    void HandleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);

    Observable<BleClientDiscovery>& ScanResults() { return mScanResults; }

private:
    BleGap();
    ~BleGap() = default;
    BleGap(const BleGap&) = delete;
    BleGap& operator=(const BleGap&) = delete;

    void ConfigureAdvertisingData(const char* deviceName);

    Observable<BleClientDiscovery> mScanResults;

    esp_ble_adv_params_t mAdvParams;
    esp_ble_scan_params_t mScanParams;
    bool mAdvertising;
    bool mScanning;
};

} // namespace Ble
} // namespace Arcana
