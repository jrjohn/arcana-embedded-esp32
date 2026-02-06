#include "BleService.hpp"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

static const char* TAG = "BleService";

/*******************************************************************************
 * extern "C" callback trampolines — forward to C++ singletons
 ******************************************************************************/

extern "C" {

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    Arcana::Ble::BleGap::Instance().HandleGapEvent(event, param);
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gattsIf,
                                 esp_ble_gatts_cb_param_t* param) {
    Arcana::Ble::BleGattServer::Instance().HandleGattsEvent(event, gattsIf, param);
}

static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                                 esp_ble_gattc_cb_param_t* param) {
    Arcana::Ble::BleGattClient::Instance().HandleGattcEvent(event, gattcIf, param);
}

} // extern "C"

namespace Arcana {
namespace Ble {

BleService& BleService::Instance() {
    static BleService sInstance;
    return sInstance;
}

esp_err_t BleService::Init() {
    if (mInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    // Release Classic BT memory (we only use BLE)
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BT classic memory release failed (may already be released): %s",
            esp_err_to_name(ret));
    }

    // Initialize BT controller in BLE mode
    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&btCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gattc_register_callback(gattc_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTC register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GATTS and GATTC app profiles (triggers REG_EVT)
    ret = BleGattServer::Instance().Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = BleGattClient::Instance().Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTC app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set preferred MTU
    ret = esp_ble_gatt_set_local_mtu(517);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set local MTU failed: %s", esp_err_to_name(ret));
    }

    // Initialize GAP (device name + adv/scan params)
#ifdef CONFIG_BLE_DEVICE_NAME
    ret = BleGap::Instance().Init(CONFIG_BLE_DEVICE_NAME);
#else
    ret = BleGap::Instance().Init("ARCANA-ESP32S3");
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    mInitialized = true;
    ESP_LOGI(TAG, "BLE Service initialized");
    return ESP_OK;
}

esp_err_t BleService::Start() {
    if (!mInitialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // Start advertising (GATT Server)
    ret = BleGap::Instance().StartAdvertising();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start advertising failed: %s", esp_err_to_name(ret));
    }

    // Start scanning (GATT Client)
#ifdef CONFIG_BLE_SCAN_DURATION
    ret = BleGap::Instance().StartScanning(CONFIG_BLE_SCAN_DURATION);
#else
    ret = BleGap::Instance().StartScanning(30);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start scanning failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "BLE Service started (advertising + scanning)");
    return ESP_OK;
}

esp_err_t BleService::Stop() {
    BleGap::Instance().StopAdvertising();
    BleGap::Instance().StopScanning();
    BleGattClient::Instance().Disconnect();

    ESP_LOGI(TAG, "BLE Service stopped");
    return ESP_OK;
}

} // namespace Ble
} // namespace Arcana
