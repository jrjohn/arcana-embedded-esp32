#pragma once

#include "BleTypes.hpp"
#include "BleUuids.hpp"
#include "Observable.hpp"

#include "esp_gatts_api.h"

namespace Arcana {
namespace Ble {

class BleGattServer {
public:
    static BleGattServer& Instance();

    esp_err_t Init();
    void HandleGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gattsIf,
                          esp_ble_gatts_cb_param_t* param);

    void UpdateTemperature(int16_t tempCenti);
    void UpdateHumidity(uint16_t humidCenti);
    void UpdateSensorStatus(uint8_t status);

    Observable<BleConnectionEvent>& ConnectionEvents() { return mConnectionEvents; }

private:
    BleGattServer();
    ~BleGattServer() = default;
    BleGattServer(const BleGattServer&) = delete;
    BleGattServer& operator=(const BleGattServer&) = delete;

    void CreateAttributeTable(esp_gatt_if_t gattsIf);
    void NotifyTemperature();
    void NotifyHumidity();

    ServerClientInfo* FindClient(uint16_t connId);
    ServerClientInfo* FindFreeSlot();
    void RemoveClient(uint16_t connId);

    Observable<BleConnectionEvent> mConnectionEvents;

    esp_gatt_if_t mGattsIf;
    uint16_t mHandleTable[ATTR_COUNT];
    ServerClientInfo mClients[kMaxServerConnections];

    int16_t mTemperature;   // Celsius * 100
    uint16_t mHumidity;     // Percent * 100
    uint8_t mSensorStatus;
};

} // namespace Ble
} // namespace Arcana
