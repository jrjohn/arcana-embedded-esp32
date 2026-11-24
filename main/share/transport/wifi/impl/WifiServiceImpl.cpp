#include "impl/WifiServiceImpl.hpp"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include <ctime>

static const char* TAG = "WifiService";

namespace Arcana::Wifi {

WifiServiceImpl& WifiServiceImpl::getInstance() {
    static WifiServiceImpl sInstance;
    return sInstance;
}

esp_err_t WifiServiceImpl::init_HAL() {
    // WiFi is initialized by esp_netif_init() + esp_event_loop in app_main
    return ESP_OK;
}

esp_err_t WifiServiceImpl::init() {
    return ESP_OK;
}

esp_err_t WifiServiceImpl::connect() {
    ESP_LOGI(TAG, "Connecting to WiFi...");
    esp_err_t ret = example_connect();
    if (ret == ESP_OK) {
        mConnected.store(true);
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

void WifiServiceImpl::disconnect() {
    example_disconnect();
    mConnected.store(false);
    ESP_LOGI(TAG, "WiFi disconnected");
}

esp_err_t WifiServiceImpl::syncNtp(uint32_t timeoutMs) {
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t ret = esp_netif_sntp_init(&sntp_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SNTP waiting for sync...");
    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeoutMs));
    if (ret == ESP_OK) {
        time_t now;
        time(&now);
        struct tm tm;
        localtime_r(&now, &tm);
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        ESP_LOGW(TAG, "SNTP sync timeout");
    }
    return ret;
}

} // namespace Arcana::Wifi
