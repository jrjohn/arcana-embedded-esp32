#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
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

/// Check if upload token "{device_id}|{expiry}|{sig}" is expired
static bool isTokenExpired(const char* token) {
    if (!token || token[0] == '\0') return true;
    const char* p = strchr(token, '|');
    if (!p) return true;
    uint32_t expiry = 0;
    for (const char* c = p + 1; *c >= '0' && *c <= '9'; c++) {
        expiry = expiry * 10 + (*c - '0');
    }
    if (expiry == 0) return true;
    time_t now;
    time(&now);
    return (now > 1577836800) && ((uint32_t)now > expiry);  // only check if NTP synced
}

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

    // Refresh token if expired (re-register to get new 30-day token)
    if (isTokenExpired(token)) {
        ESP_LOGW(TAG, "Upload token expired — refreshing");
        if (regSvc.refreshToken()) {
            token = regSvc.credentials().uploadToken;
            ESP_LOGI(TAG, "New token: %.40s...", token);
        } else {
            ESP_LOGE(TAG, "Token refresh failed — upload will likely fail");
        }
    }

    storage.pauseRecording();
    vTaskDelay(pdMS_TO_TICKS(500));

    Storage::AtsStorageServiceImpl::PendingFile pending[Storage::AtsStorageServiceImpl::MAX_PENDING];
    uint8_t count = storage.listPendingUploads(pending, Storage::AtsStorageServiceImpl::MAX_PENDING);

    uint8_t totalFiles = count + 2;
    ESP_LOGI(TAG, "%u pending + sensor.ats + device.ats = %u files", count, totalFiles);

    mProgress.totalFiles = totalFiles;
    mProgress.currentFile = 0;
    mProgress.bytesSent = 0;
    mProgress.totalBytes = 0;

    uint8_t uploaded = 0;

    for (uint8_t i = 0; i < count; i++) {
        mProgress.currentFile = uploaded + 1;
        ESP_LOGI(TAG, "Uploading %s (%u/%u)", pending[i].name, uploaded + 1, totalFiles);
        if (uploadFile(pending[i].name, deviceId, token)) {
            storage.markUploaded(pending[i].date);
            uploaded++;
        } else {
            if (Io::IoServiceImpl::getInstance().isCancelRequested()) {
                Io::IoServiceImpl::getInstance().disarmCancel();
                goto done;
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Upload current sensor.ats
    mProgress.currentFile = uploaded + 1;
    ESP_LOGI(TAG, "Uploading sensor.ats");
    if (uploadFile("sensor.ats", deviceId, token)) uploaded++;
    vTaskDelay(pdMS_TO_TICKS(500));

    // Upload device.ats
    mProgress.currentFile = uploaded + 1;
    ESP_LOGI(TAG, "Uploading device.ats");
    if (uploadFile("device.ats", deviceId, token)) uploaded++;

done:
    mProgress.currentFile = 0;
    storage.resumeRecording();
    return uploaded;
}

// ---------------------------------------------------------------------------
// Upload single file — esp_http_client streaming mode
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::uploadFile(const char* filename, const char* deviceId,
                                        const char* token) {
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    FILE* fp = fopen(filepath, "rb");
    if (!fp) { ESP_LOGW(TAG, "Cannot open %s", filepath); return false; }

    fseek(fp, 0, SEEK_END);
    uint32_t fileSize = (uint32_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fileSize == 0) { fclose(fp); return false; }

    mProgress.totalBytes = fileSize;
    mProgress.bytesSent = 0;

    // Skip files > 50MB (esp_http_client blocks on very large POST bodies)
    static const uint32_t MAX_UPLOAD_SIZE = 50 * 1024 * 1024;
    if (fileSize > MAX_UPLOAD_SIZE) {
        ESP_LOGW(TAG, "%s too large (%luMB > 50MB), skipping",
                 filename, (unsigned long)(fileSize / (1024*1024)));
        fclose(fp); return false;
    }

    // Skip resume query — upload from start (DEBUG: isolate socket reuse issue)
    uint32_t resumeOffset = 0;
    ESP_LOGI(TAG, "Upload from start (resume disabled for debug)");

    uint32_t remainSize = fileSize - resumeOffset;
    mProgress.bytesSent = resumeOffset;

    // --- esp_http_client streaming POST ---
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/upload/%s/%s",
             CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT,
             deviceId, filename);

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token);

    char rangeHeader[64];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes %lu-%lu/%lu",
             (unsigned long)resumeOffset,
             (unsigned long)(resumeOffset + remainSize - 1),
             (unsigned long)fileSize);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 512;
    cfg.buffer_size_tx = 512;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { fclose(fp); return false; }

    esp_http_client_set_header(client, "Authorization", authHeader);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "Content-Range", rangeHeader);
    esp_http_client_set_header(client, "Connection", "close");

    // Verify connectivity before upload (small GET to /health)
    {
        char healthUrl[80];
        snprintf(healthUrl, sizeof(healthUrl), "http://%s:%d/health",
                 CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT);
        esp_http_client_config_t hcfg = {};
        hcfg.url = healthUrl;
        hcfg.timeout_ms = 10000;
        auto* hc = esp_http_client_init(&hcfg);
        esp_err_t herr = esp_http_client_perform(hc);
        int hstatus = esp_http_client_get_status_code(hc);
        ESP_LOGI(TAG, "Health check: err=%s status=%d heap=%lu",
                 esp_err_to_name(herr), hstatus,
                 (unsigned long)esp_get_free_heap_size());
        esp_http_client_cleanup(hc);
        if (herr != ESP_OK || hstatus != 200) {
            ESP_LOGE(TAG, "Server unreachable — aborting upload");
            fclose(fp); return false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));  // let socket TIME_WAIT settle
    }

    // Open connection and send headers (streaming: we provide body via write())
    ESP_LOGI(TAG, "Opening upload connection (heap=%lu)...",
             (unsigned long)esp_get_free_heap_size());
    esp_err_t err = esp_http_client_open(client, (int)remainSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fclose(fp); return false;
    }
    ESP_LOGI(TAG, "Connection open OK (heap=%lu)",
             (unsigned long)esp_get_free_heap_size());

    ESP_LOGI(TAG, "POST %s (%lu bytes, offset=%lu)", filename,
             (unsigned long)remainSize, (unsigned long)resumeOffset);

    // --- Stream file body via esp_http_client_write ---
    uint8_t buf[CHUNK_SIZE];
    uint32_t sent = 0;
    bool ok = true;
    uint32_t lastLogTick = xTaskGetTickCount();

    while (sent < remainSize) {
        size_t chunkLen = (remainSize - sent > CHUNK_SIZE) ? CHUNK_SIZE : (remainSize - sent);
        size_t br = fread(buf, 1, chunkLen, fp);
        if (br == 0) {
            ESP_LOGE(TAG, "Read error at %lu", (unsigned long)(resumeOffset + sent));
            ok = false; break;
        }

        int written = esp_http_client_write(client, (const char*)buf, (int)br);
        if (written <= 0) {
            ESP_LOGE(TAG, "Write failed at %lu (err=%d, errno=%d)",
                     (unsigned long)(resumeOffset + sent), written, errno);
            ok = false; break;
        }
        sent += written;
        // Pace writes — give TCP stack time to drain buffer + receive ACKs
        vTaskDelay(pdMS_TO_TICKS(20));
        mProgress.bytesSent = resumeOffset + sent;

        notifyProgress();
        uint32_t now = xTaskGetTickCount();
        if ((now - lastLogTick) >= pdMS_TO_TICKS(5000) || sent >= remainSize) {
            uint8_t pct = (uint8_t)((mProgress.bytesSent * 100ULL) / fileSize);
            ESP_LOGI(TAG, "%s: %u%% sent=%luKB/%luKB", filename, pct,
                     (unsigned long)(mProgress.bytesSent / 1024),
                     (unsigned long)(fileSize / 1024));
            lastLogTick = now;
        }

        if (Io::IoServiceImpl::getInstance().isCancelRequested()) {
            ESP_LOGI(TAG, "Cancelled");
            ok = false; break;
        }
    }

    // --- Read HTTP response ---
    if (ok) {
        int contentLen = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ok = (status == 200 || status == 206);
        ESP_LOGI(TAG, "%s: HTTP %d (body=%d) → %s", filename, status,
                 contentLen, ok ? "OK" : "FAILED");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(fp);
    return ok;
}

// ---------------------------------------------------------------------------
// Query resume offset — streaming mode + Bearer auth
// ---------------------------------------------------------------------------

uint32_t HttpUploadServiceImpl::queryServerOffset(const char* filename,
                                                    const char* deviceId) {
    auto& regSvc = Registration::RegistrationServiceImpl::getInstance();
    const char* token = regSvc.isRegistered()
                        ? regSvc.credentials().uploadToken : "";

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/upload/%s/%s/status",
             CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT,
             deviceId, filename);

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return 0;

    esp_http_client_set_header(client, "Authorization", authHeader);

    // Use streaming mode so we can read the body after fetching headers
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "queryOffset: connect failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return 0;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    uint32_t offset = 0;
    if (status == 200) {
        char respBuf[128];
        int len = esp_http_client_read(client, respBuf, sizeof(respBuf) - 1);
        if (len > 0) {
            respBuf[len] = '\0';
            const char* p = strstr(respBuf, "\"size\":");
            if (p) {
                p += 7;
                while (*p == ' ') p++;
                while (*p >= '0' && *p <= '9') {
                    offset = offset * 10 + (*p - '0');
                    p++;
                }
            }
        }
        ESP_LOGI(TAG, "queryOffset %s: server has %lu bytes", filename,
                 (unsigned long)offset);
    } else if (status == 404) {
        ESP_LOGI(TAG, "queryOffset %s: not started (404)", filename);
        offset = 0;
    } else {
        ESP_LOGW(TAG, "queryOffset %s: status=%d", filename, status);
        offset = 0;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return offset;
}

} // namespace Arcana::Upload
