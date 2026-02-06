#include "BleGap.hpp"
#include "BleUuids.hpp"
#include "esp_log.h"

#include <cstring>

static const char* TAG = "BleGap";

namespace Arcana {
namespace Ble {

BleGap& BleGap::Instance() {
    static BleGap sInstance;
    return sInstance;
}

BleGap::BleGap()
    : mAdvParams{}
    , mScanParams{}
    , mAdvertising(false)
    , mScanning(false)
{
    // Advertising: connectable undirected, 20-40ms interval
    mAdvParams.adv_int_min       = 0x20;  // 20ms (N * 0.625ms)
    mAdvParams.adv_int_max       = 0x40;  // 40ms
    mAdvParams.adv_type          = ADV_TYPE_IND;
    mAdvParams.own_addr_type     = BLE_ADDR_TYPE_PUBLIC;
    mAdvParams.channel_map       = ADV_CHNL_ALL;
    mAdvParams.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    // Scan: active, 50ms interval, 30ms window
    mScanParams.scan_type          = BLE_SCAN_TYPE_ACTIVE;
    mScanParams.own_addr_type      = BLE_ADDR_TYPE_PUBLIC;
    mScanParams.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    mScanParams.scan_interval      = 0x50;  // 50ms
    mScanParams.scan_window        = 0x30;  // 30ms
    mScanParams.scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE;
}

esp_err_t BleGap::Init(const char* deviceName) {
    esp_err_t ret = esp_ble_gap_set_device_name(deviceName);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ConfigureAdvertisingData(deviceName);

    ret = esp_ble_gap_set_scan_params(&mScanParams);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set scan params failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

void BleGap::ConfigureAdvertisingData(const char* deviceName) {
    esp_ble_adv_data_t advData = {};
    advData.set_scan_rsp        = false;
    advData.include_name        = true;
    advData.include_txpower     = true;
    advData.min_interval        = 0x0006;  // 7.5ms
    advData.max_interval        = 0x0010;  // 20ms
    advData.appearance          = 0x0540;  // Generic Sensor
    advData.flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);

    // Include Environmental Sensing service UUID in advertising data
    static uint8_t svcUuid128[2] = {
        static_cast<uint8_t>(UUID_SVC_ENVIRONMENTAL_SENSING & 0xFF),
        static_cast<uint8_t>((UUID_SVC_ENVIRONMENTAL_SENSING >> 8) & 0xFF)
    };
    advData.service_uuid_len = sizeof(svcUuid128);
    advData.p_service_uuid   = svcUuid128;

    esp_err_t ret = esp_ble_gap_config_adv_data(&advData);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config adv data failed: %s", esp_err_to_name(ret));
    }

    // Scan response data (additional info)
    esp_ble_adv_data_t scanRsp = {};
    scanRsp.set_scan_rsp    = true;
    scanRsp.include_name    = true;
    scanRsp.include_txpower = true;

    ret = esp_ble_gap_config_adv_data(&scanRsp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config scan rsp failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t BleGap::StartAdvertising() {
    esp_err_t ret = esp_ble_gap_start_advertising(&mAdvParams);
    if (ret == ESP_OK) {
        mAdvertising = true;
        ESP_LOGI(TAG, "Advertising started");
    } else {
        ESP_LOGE(TAG, "Start advertising failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t BleGap::StopAdvertising() {
    esp_err_t ret = esp_ble_gap_stop_advertising();
    if (ret == ESP_OK) {
        mAdvertising = false;
        ESP_LOGI(TAG, "Advertising stopped");
    }
    return ret;
}

esp_err_t BleGap::StartScanning(uint32_t durationSec) {
    esp_err_t ret = esp_ble_gap_start_scanning(durationSec);
    if (ret == ESP_OK) {
        mScanning = true;
        ESP_LOGI(TAG, "Scanning started for %lu seconds", (unsigned long)durationSec);
    } else {
        ESP_LOGE(TAG, "Start scanning failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t BleGap::StopScanning() {
    esp_err_t ret = esp_ble_gap_stop_scanning();
    if (ret == ESP_OK) {
        mScanning = false;
        ESP_LOGI(TAG, "Scanning stopped");
    }
    return ret;
}

void BleGap::HandleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "ADV data set complete");
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "Scan RSP data set complete");
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising start success");
        } else {
            ESP_LOGE(TAG, "Advertising start failed, status=%d", param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        mAdvertising = false;
        ESP_LOGI(TAG, "Advertising stopped");
        break;

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "Scan params set complete");
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan start success");
        } else {
            ESP_LOGE(TAG, "Scan start failed, status=%d", param->scan_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        mScanning = false;
        ESP_LOGI(TAG, "Scan stopped");
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        auto& scanRst = param->scan_rst;
        if (scanRst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            BleClientDiscovery disc;
            memcpy(disc.Addr, scanRst.bda, sizeof(esp_bd_addr_t));
            disc.Rssi = scanRst.rssi;

            // Try to extract device name from adv data
            uint8_t* advName = nullptr;
            uint8_t nameLen = 0;
            advName = esp_ble_resolve_adv_data(scanRst.ble_adv,
                ESP_BLE_AD_TYPE_NAME_CMPL, &nameLen);
            if (advName && nameLen > 0) {
                size_t copyLen = nameLen < sizeof(disc.Name) - 1 ? nameLen : sizeof(disc.Name) - 1;
                memcpy(disc.Name, advName, copyLen);
                disc.Name[copyLen] = '\0';
            }

            ESP_LOGD(TAG, "Scan result: %02x:%02x:%02x:%02x:%02x:%02x RSSI=%d Name=%s",
                disc.Addr[0], disc.Addr[1], disc.Addr[2],
                disc.Addr[3], disc.Addr[4], disc.Addr[5],
                disc.Rssi, disc.Name);

            mScanResults.Notify(disc);
        } else if (scanRst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            ESP_LOGI(TAG, "Scan complete");
            mScanning = false;
        }
        break;
    }

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGD(TAG, "Conn params updated: status=%d, min=%d, max=%d, latency=%d, timeout=%d",
            param->update_conn_params.status,
            param->update_conn_params.min_int,
            param->update_conn_params.max_int,
            param->update_conn_params.latency,
            param->update_conn_params.timeout);
        break;

    default:
        ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

} // namespace Ble
} // namespace Arcana
