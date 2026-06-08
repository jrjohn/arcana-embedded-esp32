#pragma once

#include "BleTypes.hpp"
#include "BleUuids.hpp"
#include "Observable.hpp"

#include "esp_gattc_api.h"

namespace Arcana {
namespace Ble {

class BleGattClient {
public:
    static BleGattClient& Instance();

    esp_err_t Init();
    void HandleGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                          esp_ble_gattc_cb_param_t* param);

    esp_err_t Connect(const esp_bd_addr_t addr);
    esp_err_t Disconnect();

    Observable<BleClientDiscovery>&    DiscoveryEvents()    { return mDiscoveryEvents; }
    Observable<BleSensorNotification>& NotificationEvents() { return mNotificationEvents; }
    Observable<BleConnectionEvent>&    ConnectionEvents()   { return mConnectionEvents; }

private:
    BleGattClient();
    ~BleGattClient() = default;
    BleGattClient(const BleGattClient&) = delete;
    BleGattClient& operator=(const BleGattClient&) = delete;

    void SearchService(esp_gatt_if_t gattcIf, uint16_t connId);
    void RegisterForNotify(esp_gatt_if_t gattcIf, uint16_t connId, uint16_t charHandle);
    void WriteCccd(esp_gatt_if_t gattcIf, uint16_t connId, uint16_t cccdHandle, bool enable);

    Observable<BleClientDiscovery>    mDiscoveryEvents;
    Observable<BleSensorNotification> mNotificationEvents;
    Observable<BleConnectionEvent>    mConnectionEvents;

    esp_gatt_if_t mGattcIf;
    uint16_t mConnId;
    bool mConnected;

    // Discovered handles for the remote Environmental Sensing service
    uint16_t mServiceStartHandle;
    uint16_t mServiceEndHandle;
    uint16_t mTempCharHandle;
    uint16_t mTempCccdHandle;
    uint16_t mHumidCharHandle;
    uint16_t mHumidCccdHandle;

    esp_bd_addr_t mRemoteAddr;
};

} // namespace Ble
} // namespace Arcana
