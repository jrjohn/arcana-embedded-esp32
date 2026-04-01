#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "HttpUpload";

#ifndef CONFIG_UPLOAD_SERVER_HOST
#define CONFIG_UPLOAD_SERVER_HOST  "arcana.boo"
#endif
#ifndef CONFIG_UPLOAD_SERVER_PORT
#define CONFIG_UPLOAD_SERVER_PORT  8088
#endif

static const char* MOUNT_POINT = "/sdcard";
static const size_t CHUNK_SIZE = 2048;

namespace Arcana::Upload {

HttpUploadServiceImpl& HttpUploadServiceImpl::getInstance() {
    static HttpUploadServiceImpl sInstance;
    return sInstance;
}

// ---------------------------------------------------------------------------
// Upload all pending files
// ---------------------------------------------------------------------------

uint8_t HttpUploadServiceImpl::uploadPendingFiles() {
    auto& storage = static_cast<Storage::AtsStorageServiceImpl&>(
        Storage::AtsStorageServiceImpl::getInstance());

    if (!storage.isReady()) {
        ESP_LOGW(TAG, "Storage not ready");
        return 0;
    }

    auto& regSvc = Registration::RegistrationServiceImpl::getInstance();
    const char* deviceId = regSvc.deviceId();
    const char* token = regSvc.isRegistered()
                        ? regSvc.credentials().uploadToken : "";

    // Pause ATS recording (FatFS not thread-safe)
    storage.pauseRecording();
    vTaskDelay(pdMS_TO_TICKS(500));

    // List pending files
    Storage::AtsStorageServiceImpl::PendingFile pending[Storage::AtsStorageServiceImpl::MAX_PENDING];
    uint8_t count = storage.listPendingUploads(pending, Storage::AtsStorageServiceImpl::MAX_PENDING);

    // Always upload sensor.ats (current day) + device.ats + any rotated files
    uint8_t totalFiles = count + 2;  // pending + sensor.ats + device.ats
    ESP_LOGI(TAG, "%u pending + sensor.ats + device.ats = %u files", count, totalFiles);

    mProgress.totalFiles = totalFiles;
    mProgress.currentFile = 0;
    mProgress.bytesSent = 0;
    mProgress.totalBytes = 0;

    uint8_t uploaded = 0;

    // 1. Upload rotated YYYYMMDD.ats files
    for (uint8_t i = 0; i < count; i++) {
        mProgress.currentFile = uploaded + 1;
        ESP_LOGI(TAG, "Uploading %s (%u/%u)", pending[i].name, uploaded + 1, totalFiles);

        if (uploadFile(pending[i].name, deviceId, token)) {
            storage.markUploaded(pending[i].date);
            uploaded++;
        } else {
            ESP_LOGW(TAG, "Upload failed: %s", pending[i].name);
            if (Io::IoServiceImpl::getInstance().isCancelRequested()) {
                Io::IoServiceImpl::getInstance().disarmCancel();
                goto done;
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 2. Upload current day sensor.ats
    mProgress.currentFile = uploaded + 1;
    ESP_LOGI(TAG, "Uploading sensor.ats (current day)");
    if (uploadFile("sensor.ats", deviceId, token)) {
        uploaded++;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. Upload device.ats (lifecycle/config/credentials)
    mProgress.currentFile = uploaded + 1;
    ESP_LOGI(TAG, "Uploading device.ats");
    if (uploadFile("device.ats", deviceId, token)) {
        uploaded++;
    }

done:
    mProgress.currentFile = 0;
    storage.resumeRecording();

    return uploaded;
}

// ---------------------------------------------------------------------------
// Upload single file
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::uploadFile(const char* filename, const char* deviceId,
                                        const char* token) {
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Cannot open %s", filepath);
        return false;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    uint32_t fileSize = (uint32_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize == 0) { fclose(fp); return false; }

    mProgress.totalBytes = fileSize;
    mProgress.bytesSent = 0;

    // Query server for resume offset
    uint32_t resumeOffset = queryServerOffset(filename, deviceId);
    if (resumeOffset >= fileSize) {
        ESP_LOGI(TAG, "%s already uploaded (%lu bytes)", filename, (unsigned long)fileSize);
        fclose(fp);
        return true;
    }

    if (resumeOffset > 0) {
        ESP_LOGI(TAG, "Resuming %s from %lu/%lu", filename,
                 (unsigned long)resumeOffset, (unsigned long)fileSize);
        fseek(fp, resumeOffset, SEEK_SET);
    }

    uint32_t remainSize = fileSize - resumeOffset;

    // Build URL
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/upload/%s/%s",
             CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT,
             deviceId, filename);

    // Build Content-Range header
    char rangeHeader[64];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes %lu-%lu/%lu",
             (unsigned long)resumeOffset,
             (unsigned long)(fileSize - 1),
             (unsigned long)fileSize);

    // Build Authorization header
    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 600000;  // 10 minutes — large files need time

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "Authorization", authHeader);
    esp_http_client_set_header(client, "Content-Range", rangeHeader);

    esp_err_t err = esp_http_client_open(client, (int)remainSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fclose(fp);
        return false;
    }

    // Stream file body
    uint8_t buf[CHUNK_SIZE];
    uint32_t sent = 0;
    bool ok = true;

    while (sent < remainSize) {
        uint32_t remaining = remainSize - sent;
        size_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : (size_t)remaining;

        size_t br = fread(buf, 1, chunkLen, fp);
        if (br == 0) {
            ESP_LOGE(TAG, "File read error at %lu", (unsigned long)(resumeOffset + sent));
            ok = false;
            break;
        }

        int written = esp_http_client_write(client, (const char*)buf, (int)br);
        if (written <= 0) {
            ESP_LOGE(TAG, "HTTP write failed at %lu", (unsigned long)(resumeOffset + sent));
            ok = false;
            break;
        }

        sent += written;
        mProgress.bytesSent = resumeOffset + sent;

        // Progress log every ~20KB
        if (sent % (CHUNK_SIZE * 10) == 0 || sent >= remainSize) {
            uint8_t pct = (uint8_t)((mProgress.bytesSent * 100ULL) / fileSize);
            ESP_LOGI(TAG, "%s: %u%% (%lu/%lu)", filename, pct,
                     (unsigned long)mProgress.bytesSent, (unsigned long)fileSize);
        }

        // Cancel check
        if (Io::IoServiceImpl::getInstance().isCancelRequested()) {
            ESP_LOGI(TAG, "Upload cancelled");
            ok = false;
            break;
        }
    }

    // Read response
    if (ok) {
        int contentLen = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ok = (status >= 200 && status < 300);
        ESP_LOGI(TAG, "%s: HTTP %d, content=%d", filename, status, contentLen);
    }

    esp_http_client_cleanup(client);
    fclose(fp);

    return ok;
}

// ---------------------------------------------------------------------------
// Query server for resume offset
// ---------------------------------------------------------------------------

uint32_t HttpUploadServiceImpl::queryServerOffset(const char* filename,
                                                    const char* deviceId) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/upload/%s/%s/status",
             CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT,
             deviceId, filename);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    uint32_t offset = 0;
    if (err == ESP_OK && status == 200) {
        char respBuf[128];
        int len = esp_http_client_read(client, respBuf, sizeof(respBuf) - 1);
        if (len > 0) {
            respBuf[len] = '\0';
            const char* sizeStr = strstr(respBuf, "\"size\":");
            if (sizeStr) {
                sizeStr += 7;
                while (*sizeStr == ' ') sizeStr++;
                while (*sizeStr >= '0' && *sizeStr <= '9') {
                    offset = offset * 10 + (*sizeStr - '0');
                    sizeStr++;
                }
            }
        }
    }

    esp_http_client_cleanup(client);
    return offset;
}

} // namespace Arcana::Upload
