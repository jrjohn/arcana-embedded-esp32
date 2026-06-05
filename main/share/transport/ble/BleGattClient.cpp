#include "BleGattClient.hpp"
#include "esp_log.h"

#include <cstring>

static const char* TAG = "BleGattClient";

namespace Arcana {
namespace Ble {

BleGattClient& BleGattClient::Instance() {
    static BleGattClient sInstance;
    return sInstance;
}

BleGattClient::BleGattClient()
    : mGattcIf(ESP_GATT_IF_NONE)
    , mConnId(0)
    , mConnected(false)
    , mServiceStartHandle(0)
    , mServiceEndHandle(0)
    , mTempCharHandle(0)
    , mTempCccdHandle(0)
    , mHumidCharHandle(0)
    , mHumidCccdHandle(0)
{
    memset(mRemoteAddr, 0, sizeof(esp_bd_addr_t));
}

esp_err_t BleGattClient::Init() {
    return esp_ble_gattc_app_register(1);
}

esp_err_t BleGattClient::Connect(const esp_bd_addr_t addr) {
    memcpy(mRemoteAddr, addr, sizeof(esp_bd_addr_t));
    return esp_ble_gattc_open(mGattcIf, const_cast<uint8_t*>(addr), BLE_ADDR_TYPE_PUBLIC, true);
}

esp_err_t BleGattClient::Disconnect() {
    if (!mConnected) return ESP_ERR_INVALID_STATE;
    return esp_ble_gattc_close(mGattcIf, mConnId);
}

void BleGattClient::HandleGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                                      esp_ble_gattc_cb_param_t* param) {
    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            mGattcIf = gattcIf;
            ESP_LOGI(TAG, "GATTC app registered, app_id=%d", param->reg.app_id);
        } else {
            ESP_LOGE(TAG, "GATTC register failed, status=%d", param->reg.status);
        }
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
            mConnId = param->open.conn_id;
            mConnected = true;
            ESP_LOGI(TAG, "Connected to remote device, conn_id=%d", mConnId);

            // Negotiate MTU
            esp_ble_gattc_send_mtu_req(gattcIf, mConnId);

            BleConnectionEvent evt;
            evt.Role = BleRole::Client;
            evt.State = ConnectionState::Connected;
            evt.ConnId = mConnId;
            memcpy(evt.RemoteAddr, mRemoteAddr, sizeof(esp_bd_addr_t));
            mConnectionEvents.Notify(evt);
        } else {
            ESP_LOGE(TAG, "Open failed, status=%d", param->open.status);
            mConnected = false;
        }
        break;

    case ESP_GATTC_CLOSE_EVT: {
        mConnected = false;
        ESP_LOGI(TAG, "Disconnected from remote, conn_id=%d, reason=0x%x",
            param->close.conn_id, param->close.reason);

        BleConnectionEvent evt;
        evt.Role = BleRole::Client;
        evt.State = ConnectionState::Disconnected;
        evt.ConnId = param->close.conn_id;
        memcpy(evt.RemoteAddr, param->close.remote_bda, sizeof(esp_bd_addr_t));
        mConnectionEvents.Notify(evt);

        // Reset discovered handles
        mServiceStartHandle = 0;
        mServiceEndHandle = 0;
        mTempCharHandle = 0;
        mTempCccdHandle = 0;
        mHumidCharHandle = 0;
        mHumidCccdHandle = 0;
        break;
    }

    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "MTU configured: %d", param->cfg_mtu.mtu);
        } else {
            ESP_LOGW(TAG, "MTU config failed, status=%d", param->cfg_mtu.status);
        }
        // After MTU negotiation, start service discovery
        SearchService(gattcIf, mConnId);
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        // Found a service on the remote device
        auto& svcUuid = param->search_res.srvc_id.uuid;
        if (svcUuid.len == ESP_UUID_LEN_16 && svcUuid.uuid.uuid16 == UUID_SVC_ENVIRONMENTAL_SENSING) {
            mServiceStartHandle = param->search_res.start_handle;
            mServiceEndHandle = param->search_res.end_handle;
            ESP_LOGI(TAG, "Found Environmental Sensing service: start=%d end=%d",
                mServiceStartHandle, mServiceEndHandle);
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (param->search_cmpl.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Service discovery complete");
            if (mServiceStartHandle != 0) {
                // Discover characteristics within the service
                uint16_t count = 0;
                esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
                    gattcIf, mConnId, ESP_GATT_DB_CHARACTERISTIC,
                    mServiceStartHandle, mServiceEndHandle, 0, &count);

                if (status == ESP_GATT_OK && count > 0) {
                    auto* charResult = new esp_gattc_char_elem_t[count];
                    status = esp_ble_gattc_get_all_char(
                        gattcIf, mConnId,
                        mServiceStartHandle, mServiceEndHandle,
                        charResult, &count, 0);

                    if (status == ESP_GATT_OK) {
                        for (uint16_t i = 0; i < count; i++) {
                            if (charResult[i].uuid.len == ESP_UUID_LEN_16) {
                                uint16_t uuid16 = charResult[i].uuid.uuid.uuid16;
                                if (uuid16 == UUID_CHAR_TEMPERATURE) {
                                    mTempCharHandle = charResult[i].char_handle;
                                    ESP_LOGI(TAG, "Found Temperature char, handle=%d", mTempCharHandle);
                                } else if (uuid16 == UUID_CHAR_HUMIDITY) {
                                    mHumidCharHandle = charResult[i].char_handle;
                                    ESP_LOGI(TAG, "Found Humidity char, handle=%d", mHumidCharHandle);
                                }
                            }
                        }
                    }
                    delete[] charResult;
                }

                // Discover CCCDs for characteristics that support notify
                if (mTempCharHandle != 0) {
                    uint16_t descCount = 0;
                    esp_ble_gattc_get_attr_count(
                        gattcIf, mConnId, ESP_GATT_DB_DESCRIPTOR,
                        mTempCharHandle, mServiceEndHandle, mTempCharHandle, &descCount);

                    if (descCount > 0) {
                        auto* descResult = new esp_gattc_descr_elem_t[descCount];
                        esp_bt_uuid_t cccdUuid;
                        cccdUuid.len = ESP_UUID_LEN_16;
                        cccdUuid.uuid.uuid16 = UUID_DESC_CCCD;

                        esp_ble_gattc_get_descr_by_char_handle(
                            gattcIf, mConnId, mTempCharHandle, cccdUuid,
                            descResult, &descCount);

                        if (descCount > 0) {
                            mTempCccdHandle = descResult[0].handle;
                            ESP_LOGI(TAG, "Found Temperature CCCD, handle=%d", mTempCccdHandle);
                        }
                        delete[] descResult;
                    }

                    // Register for notification
                    RegisterForNotify(gattcIf, mConnId, mTempCharHandle);
                }

                if (mHumidCharHandle != 0) {
                    uint16_t descCount = 0;
                    esp_ble_gattc_get_attr_count(
                        gattcIf, mConnId, ESP_GATT_DB_DESCRIPTOR,
                        mHumidCharHandle, mServiceEndHandle, mHumidCharHandle, &descCount);

                    if (descCount > 0) {
                        auto* descResult = new esp_gattc_descr_elem_t[descCount];
                        esp_bt_uuid_t cccdUuid;
                        cccdUuid.len = ESP_UUID_LEN_16;
                        cccdUuid.uuid.uuid16 = UUID_DESC_CCCD;

                        esp_ble_gattc_get_descr_by_char_handle(
                            gattcIf, mConnId, mHumidCharHandle, cccdUuid,
                            descResult, &descCount);

                        if (descCount > 0) {
                            mHumidCccdHandle = descResult[0].handle;
                            ESP_LOGI(TAG, "Found Humidity CCCD, handle=%d", mHumidCccdHandle);
                        }
                        delete[] descResult;
                    }

                    RegisterForNotify(gattcIf, mConnId, mHumidCharHandle);
                }
            }
        } else {
            ESP_LOGE(TAG, "Service discovery failed, status=%d", param->search_cmpl.status);
        }
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (param->reg_for_notify.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Registered for notify, handle=%d", param->reg_for_notify.handle);

            // Write CCCD to enable notifications on the remote device
            if (param->reg_for_notify.handle == mTempCharHandle && mTempCccdHandle != 0) {
                WriteCccd(gattcIf, mConnId, mTempCccdHandle, true);
            } else if (param->reg_for_notify.handle == mHumidCharHandle && mHumidCccdHandle != 0) {
                WriteCccd(gattcIf, mConnId, mHumidCccdHandle, true);
            }
        } else {
            ESP_LOGE(TAG, "Register for notify failed, status=%d", param->reg_for_notify.status);
        }
        break;

    case ESP_GATTC_NOTIFY_EVT: {
        ESP_LOGD(TAG, "Notify received: handle=%d, len=%d",
            param->notify.handle, param->notify.value_len);

        BleSensorNotification notif;
        memcpy(notif.RemoteAddr, mRemoteAddr, sizeof(esp_bd_addr_t));

        if (param->notify.handle == mTempCharHandle) {
            notif.CharUuid = UUID_CHAR_TEMPERATURE;
        } else if (param->notify.handle == mHumidCharHandle) {
            notif.CharUuid = UUID_CHAR_HUMIDITY;
        } else {
            notif.CharUuid = 0;
        }

        uint16_t copyLen = param->notify.value_len < sizeof(notif.Data) ? param->notify.value_len : sizeof(notif.Data);
        memcpy(notif.Data, param->notify.value, copyLen);
        notif.DataLen = copyLen;

        mNotificationEvents.Notify(notif);
        break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Descriptor write success, handle=%d", param->write.handle);
        } else {
            ESP_LOGW(TAG, "Descriptor write failed, status=%d", param->write.status);
        }
        break;

    default:
        ESP_LOGD(TAG, "Unhandled GATTC event: %d", event);
        break;
    }
}

void BleGattClient::SearchService(esp_gatt_if_t gattcIf, uint16_t connId) {
    esp_bt_uuid_t svcUuid;
    svcUuid.len = ESP_UUID_LEN_16;
    svcUuid.uuid.uuid16 = UUID_SVC_ENVIRONMENTAL_SENSING;

    esp_err_t ret = esp_ble_gattc_search_service(gattcIf, connId, &svcUuid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Search service failed: %s", esp_err_to_name(ret));
    }
}

void BleGattClient::RegisterForNotify(esp_gatt_if_t gattcIf, uint16_t /*connId*/, uint16_t charHandle) {
    esp_err_t ret = esp_ble_gattc_register_for_notify(gattcIf, mRemoteAddr, charHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register for notify failed: %s", esp_err_to_name(ret));
    }
}

void BleGattClient::WriteCccd(esp_gatt_if_t gattcIf, uint16_t connId, uint16_t cccdHandle, bool enable) {
    uint16_t cccdVal = enable ? 0x0001 : 0x0000;
    esp_err_t ret = esp_ble_gattc_write_char_descr(
        gattcIf, connId, cccdHandle,
        sizeof(cccdVal), reinterpret_cast<uint8_t*>(&cccdVal),
        ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write CCCD failed: %s", esp_err_to_name(ret));
    }
}

} // namespace Ble
} // namespace Arcana
