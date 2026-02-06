#include "BleGattServer.hpp"
#include "BleGap.hpp"
#include "esp_log.h"

#include <cstring>

static const char* TAG = "BleGattServer";

namespace Arcana {
namespace Ble {

/*******************************************************************************
 * GATT Attribute Table Definition
 ******************************************************************************/

// Attribute values for declarations
static const uint16_t sPrimarySvcUuid   = UUID_PRIMARY_SERVICE;
static const uint16_t sCharDeclUuid     = UUID_CHAR_DECLARE;
static const uint16_t sCccdUuid         = UUID_DESC_CCCD;

// Service UUID
static const uint16_t sEnvSensingUuid   = UUID_SVC_ENVIRONMENTAL_SENSING;

// Characteristic UUIDs
static const uint16_t sTempUuid         = UUID_CHAR_TEMPERATURE;
static const uint16_t sHumidUuid        = UUID_CHAR_HUMIDITY;
static const uint16_t sStatusUuid       = UUID_CHAR_SENSOR_STATUS;

// Characteristic properties
static const uint8_t sCharPropReadNotify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t sCharPropRead       = ESP_GATT_CHAR_PROP_BIT_READ;

// Default CCCD value (notifications disabled)
static uint16_t sCccdDefaultVal = 0x0000;

// Default characteristic values
static int16_t  sTempDefaultVal   = 0;
static uint16_t sHumidDefaultVal  = 0;
static uint8_t  sStatusDefaultVal = 0;

// Full GATT attribute table
static const esp_gatts_attr_db_t sAttrTable[ATTR_COUNT] = {
    // Service Declaration
    [ATTR_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sPrimarySvcUuid,
            ESP_GATT_PERM_READ,
            sizeof(uint16_t), sizeof(sEnvSensingUuid),
            (uint8_t*)&sEnvSensingUuid
        }
    },

    // Temperature Characteristic Declaration
    [ATTR_TEMP_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sCharDeclUuid,
            ESP_GATT_PERM_READ,
            sizeof(uint8_t), sizeof(sCharPropReadNotify),
            (uint8_t*)&sCharPropReadNotify
        }
    },
    // Temperature Value
    [ATTR_TEMP_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sTempUuid,
            ESP_GATT_PERM_READ,
            sizeof(int16_t), sizeof(sTempDefaultVal),
            (uint8_t*)&sTempDefaultVal
        }
    },
    // Temperature CCCD
    [ATTR_TEMP_CCCD] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sCccdUuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(sCccdDefaultVal),
            (uint8_t*)&sCccdDefaultVal
        }
    },

    // Humidity Characteristic Declaration
    [ATTR_HUMID_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sCharDeclUuid,
            ESP_GATT_PERM_READ,
            sizeof(uint8_t), sizeof(sCharPropReadNotify),
            (uint8_t*)&sCharPropReadNotify
        }
    },
    // Humidity Value
    [ATTR_HUMID_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sHumidUuid,
            ESP_GATT_PERM_READ,
            sizeof(uint16_t), sizeof(sHumidDefaultVal),
            (uint8_t*)&sHumidDefaultVal
        }
    },
    // Humidity CCCD
    [ATTR_HUMID_CCCD] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sCccdUuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(sCccdDefaultVal),
            (uint8_t*)&sCccdDefaultVal
        }
    },

    // Sensor Status Characteristic Declaration
    [ATTR_STATUS_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sCharDeclUuid,
            ESP_GATT_PERM_READ,
            sizeof(uint8_t), sizeof(sCharPropRead),
            (uint8_t*)&sCharPropRead
        }
    },
    // Sensor Status Value
    [ATTR_STATUS_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16, (uint8_t*)&sStatusUuid,
            ESP_GATT_PERM_READ,
            sizeof(uint8_t), sizeof(sStatusDefaultVal),
            (uint8_t*)&sStatusDefaultVal
        }
    },
};

/*******************************************************************************
 * Implementation
 ******************************************************************************/

BleGattServer& BleGattServer::Instance() {
    static BleGattServer sInstance;
    return sInstance;
}

BleGattServer::BleGattServer()
    : mGattsIf(ESP_GATT_IF_NONE)
    , mHandleTable{}
    , mClients{}
    , mTemperature(0)
    , mHumidity(0)
    , mSensorStatus(0)
{
}

esp_err_t BleGattServer::Init() {
    return esp_ble_gatts_app_register(0);
}

void BleGattServer::CreateAttributeTable(esp_gatt_if_t gattsIf) {
    esp_err_t ret = esp_ble_gatts_create_attr_tab(sAttrTable, gattsIf, ATTR_COUNT, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Create attr table failed: %s", esp_err_to_name(ret));
    }
}

void BleGattServer::HandleGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gattsIf,
                                      esp_ble_gatts_cb_param_t* param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            mGattsIf = gattsIf;
            ESP_LOGI(TAG, "GATTS app registered, app_id=%d", param->reg.app_id);
            CreateAttributeTable(gattsIf);
        } else {
            ESP_LOGE(TAG, "GATTS register failed, status=%d", param->reg.status);
        }
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle == ATTR_COUNT) {
            memcpy(mHandleTable, param->add_attr_tab.handles, sizeof(mHandleTable));
            esp_ble_gatts_start_service(mHandleTable[ATTR_SVC]);
            ESP_LOGI(TAG, "Attribute table created, handles=%d", param->add_attr_tab.num_handle);
        } else {
            ESP_LOGE(TAG, "Create attr tab failed, status=%d, num_handle=%d",
                param->add_attr_tab.status, param->add_attr_tab.num_handle);
        }
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "Service started, handle=%d", param->start.service_handle);
        break;

    case ESP_GATTS_CONNECT_EVT: {
        ESP_LOGI(TAG, "Client connected, conn_id=%d, addr=%02x:%02x:%02x:%02x:%02x:%02x",
            param->connect.conn_id,
            param->connect.remote_bda[0], param->connect.remote_bda[1],
            param->connect.remote_bda[2], param->connect.remote_bda[3],
            param->connect.remote_bda[4], param->connect.remote_bda[5]);

        ServerClientInfo* slot = FindFreeSlot();
        if (slot) {
            slot->Connected = true;
            slot->ConnId = param->connect.conn_id;
            memcpy(slot->Addr, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            slot->TempCccdEnabled = false;
            slot->HumidCccdEnabled = false;
        } else {
            ESP_LOGW(TAG, "Max clients reached, no slot available");
        }

        // Update connection parameters
        esp_ble_conn_update_params_t connParams = {};
        memcpy(connParams.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        connParams.latency  = 0;
        connParams.max_int  = 0x20;  // 40ms
        connParams.min_int  = 0x10;  // 20ms
        connParams.timeout  = 400;   // 4s
        esp_ble_gap_update_conn_params(&connParams);

        BleConnectionEvent evt;
        evt.Role = BleRole::Server;
        evt.State = ConnectionState::Connected;
        evt.ConnId = param->connect.conn_id;
        memcpy(evt.RemoteAddr, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        mConnectionEvents.Notify(evt);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        ESP_LOGI(TAG, "Client disconnected, conn_id=%d, reason=0x%x",
            param->disconnect.conn_id, param->disconnect.reason);

        RemoveClient(param->disconnect.conn_id);

        BleConnectionEvent evt;
        evt.Role = BleRole::Server;
        evt.State = ConnectionState::Disconnected;
        evt.ConnId = param->disconnect.conn_id;
        memcpy(evt.RemoteAddr, param->disconnect.remote_bda, sizeof(esp_bd_addr_t));
        mConnectionEvents.Notify(evt);

        // Restart advertising after disconnect
        BleGap::Instance().StartAdvertising();
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        uint16_t handle = param->write.handle;

        // Check if writing to Temperature CCCD
        if (handle == mHandleTable[ATTR_TEMP_CCCD]) {
            if (param->write.len == 2) {
                uint16_t cccdVal = param->write.value[0] | (param->write.value[1] << 8);
                ServerClientInfo* client = FindClient(param->write.conn_id);
                if (client) {
                    client->TempCccdEnabled = (cccdVal == 0x0001);
                    ESP_LOGI(TAG, "Temperature CCCD: conn_id=%d enabled=%d",
                        param->write.conn_id, client->TempCccdEnabled);
                }
            }
        }
        // Check if writing to Humidity CCCD
        else if (handle == mHandleTable[ATTR_HUMID_CCCD]) {
            if (param->write.len == 2) {
                uint16_t cccdVal = param->write.value[0] | (param->write.value[1] << 8);
                ServerClientInfo* client = FindClient(param->write.conn_id);
                if (client) {
                    client->HumidCccdEnabled = (cccdVal == 0x0001);
                    ESP_LOGI(TAG, "Humidity CCCD: conn_id=%d enabled=%d",
                        param->write.conn_id, client->HumidCccdEnabled);
                }
            }
        }

        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gattsIf, param->write.conn_id,
                param->write.trans_id, ESP_GATT_OK, nullptr);
        }
        break;
    }

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU set to %d, conn_id=%d", param->mtu.mtu, param->mtu.conn_id);
        break;

    case ESP_GATTS_CONF_EVT:
        if (param->conf.status != ESP_GATT_OK) {
            ESP_LOGW(TAG, "Indication confirm failed, status=%d", param->conf.status);
        }
        break;

    default:
        ESP_LOGD(TAG, "Unhandled GATTS event: %d", event);
        break;
    }
}

void BleGattServer::UpdateTemperature(int16_t tempCenti) {
    mTemperature = tempCenti;

    // Update attribute value in local database
    esp_ble_gatts_set_attr_value(mHandleTable[ATTR_TEMP_VAL],
        sizeof(mTemperature), (uint8_t*)&mTemperature);

    NotifyTemperature();
}

void BleGattServer::UpdateHumidity(uint16_t humidCenti) {
    mHumidity = humidCenti;

    esp_ble_gatts_set_attr_value(mHandleTable[ATTR_HUMID_VAL],
        sizeof(mHumidity), (uint8_t*)&mHumidity);

    NotifyHumidity();
}

void BleGattServer::UpdateSensorStatus(uint8_t status) {
    mSensorStatus = status;

    esp_ble_gatts_set_attr_value(mHandleTable[ATTR_STATUS_VAL],
        sizeof(mSensorStatus), &mSensorStatus);
}

void BleGattServer::NotifyTemperature() {
    if (mGattsIf == ESP_GATT_IF_NONE) return;

    for (auto& client : mClients) {
        if (client.Connected && client.TempCccdEnabled) {
            esp_ble_gatts_send_indicate(mGattsIf, client.ConnId,
                mHandleTable[ATTR_TEMP_VAL],
                sizeof(mTemperature), (uint8_t*)&mTemperature, false);
        }
    }
}

void BleGattServer::NotifyHumidity() {
    if (mGattsIf == ESP_GATT_IF_NONE) return;

    for (auto& client : mClients) {
        if (client.Connected && client.HumidCccdEnabled) {
            esp_ble_gatts_send_indicate(mGattsIf, client.ConnId,
                mHandleTable[ATTR_HUMID_VAL],
                sizeof(mHumidity), (uint8_t*)&mHumidity, false);
        }
    }
}

ServerClientInfo* BleGattServer::FindClient(uint16_t connId) {
    for (auto& client : mClients) {
        if (client.Connected && client.ConnId == connId) {
            return &client;
        }
    }
    return nullptr;
}

ServerClientInfo* BleGattServer::FindFreeSlot() {
    for (auto& client : mClients) {
        if (!client.Connected) {
            return &client;
        }
    }
    return nullptr;
}

void BleGattServer::RemoveClient(uint16_t connId) {
    for (auto& client : mClients) {
        if (client.Connected && client.ConnId == connId) {
            client = ServerClientInfo();
            return;
        }
    }
}

} // namespace Ble
} // namespace Arcana
