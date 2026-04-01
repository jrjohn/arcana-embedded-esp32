#include "impl/OtaServiceImpl.hpp"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "OtaService";

namespace Arcana {

OtaServiceImpl& OtaServiceImpl::getInstance() {
    static OtaServiceImpl sInstance;
    return sInstance;
}

bool OtaServiceImpl::startUpdate(const char* host, uint16_t port,
                                  const char* path, uint32_t expectedSize,
                                  uint32_t expectedCrc32) {
    if (mActive) return false;

    mActive = true;
    mProgress = 0;

    // Build URL
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%u%s", host, port, path);
    ESP_LOGI(TAG, "OTA starting: %s (size=%lu, crc32=0x%08lx)",
             url, (unsigned long)expectedSize, (unsigned long)expectedCrc32);

    // Configure HTTP client
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.timeout_ms = 30000;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        mActive = false;
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        mActive = false;
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_cleanup(client);
        mActive = false;
        return false;
    }

    // Begin OTA
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        esp_http_client_cleanup(client);
        mActive = false;
        return false;
    }

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        mActive = false;
        return false;
    }

    // Download + write
    uint8_t buf[1024];
    uint32_t bytesWritten = 0;

    while (bytesWritten < (uint32_t)content_length) {
        int readLen = esp_http_client_read(client, (char*)buf, sizeof(buf));
        if (readLen <= 0) {
            ESP_LOGE(TAG, "HTTP read failed at %lu bytes", (unsigned long)bytesWritten);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            mActive = false;
            return false;
        }

        err = esp_ota_write(ota_handle, buf, readLen);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            mActive = false;
            return false;
        }

        bytesWritten += readLen;
        if (content_length > 0) {
            mProgress = (uint8_t)((bytesWritten * 100UL) / content_length);
        }
    }

    esp_http_client_cleanup(client);

    // Finalize
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        mActive = false;
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        mActive = false;
        return false;
    }

    mProgress = 100;
    ESP_LOGI(TAG, "OTA complete (%lu bytes), restarting...",
             (unsigned long)bytesWritten);

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

    return true;  // never reached
}

} // namespace Arcana
