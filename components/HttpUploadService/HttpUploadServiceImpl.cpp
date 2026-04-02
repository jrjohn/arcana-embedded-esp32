#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "HttpUpload";

#ifndef CONFIG_UPLOAD_SERVER_HOST
#define CONFIG_UPLOAD_SERVER_HOST  "arcana.boo"
#endif
#ifndef CONFIG_UPLOAD_SERVER_PORT
#define CONFIG_UPLOAD_SERVER_PORT  443
#endif

static const char* MOUNT_POINT = "/sdcard";
static const size_t CHUNK_SIZE = 2048;

// ECC P-256 self-signed cert for local HTTPS test server (611 bytes vs RSA 1147)
static const char LOCAL_SERVER_CERT[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIBlzCCATygAwIBAgIUHOkyHLjbJ7t1s++opThEv88gI80wCgYIKoZIzj0EAwIw\n"
"GDEWMBQGA1UEAwwNMTkyLjE2OC4xMS40NDAeFw0yNjA0MDIxNDAyMzFaFw0yNzA0\n"
"MDIxNDAyMzFaMBgxFjAUBgNVBAMMDTE5Mi4xNjguMTEuNDQwWTATBgcqhkjOPQIB\n"
"BggqhkjOPQMBBwNCAATf2jDGtUWlg0llL7/DqAp5QNKtEjQA6/EwTZIOsdUZrDB0\n"
"3AcpAUnA6MvNC79RosYnYtMtPrmC0UgajyVDtV29o2QwYjAdBgNVHQ4EFgQU9WW4\n"
"O/NhIMTpkirAyMelDOgHto8wHwYDVR0jBBgwFoAU9WW4O/NhIMTpkirAyMelDOgH\n"
"to8wDwYDVR0TAQH/BAUwAwEB/zAPBgNVHREECDAGhwTAqAssMAoGCCqGSM49BAMC\n"
"A0kAMEYCIQDtzsA/80iLWDPMK5SXGixYoq9YeBw93lqm7BHhuz14/gIhAMcJrTZm\n"
"Z9Pm0yYfG5F8r/fbNWWhHB0Z6DpnDHrxyvDe\n"
"-----END CERTIFICATE-----\n";

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
// Upload single file — MINIMAL esp_http_client test (no auth, no resume)
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

    // Skip files > 50MB
    if (fileSize > 50 * 1024 * 1024) {
        ESP_LOGW(TAG, "%s too large (%luMB), skipping",
                 filename, (unsigned long)(fileSize / (1024*1024)));
        fclose(fp); return false;
    }

    mProgress.totalBytes = fileSize;
    mProgress.bytesSent = 0;

    // --- POST with HTTPS + Bearer auth ---
    char url[128];
    snprintf(url, sizeof(url), "https://%s:%d/upload/%s/%s",
             CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT,
             deviceId, filename);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 30000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;  // Let's Encrypt via ESP-IDF bundle

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { fclose(fp); return false; }

    // Auth header
    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", authHeader);

    ESP_LOGI(TAG, "open(%s, %lu bytes) heap=%lu", filename,
             (unsigned long)fileSize, (unsigned long)esp_get_free_heap_size());

    esp_err_t err = esp_http_client_open(client, (int)fileSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client); fclose(fp); return false;
    }

    ESP_LOGI(TAG, "open OK, heap=%lu, writing...",
             (unsigned long)esp_get_free_heap_size());

    // Stream body
    uint8_t buf[1024];  // smaller chunk
    uint32_t sent = 0;
    bool ok = true;

    while (sent < fileSize) {
        size_t want = (fileSize - sent > sizeof(buf)) ? sizeof(buf) : (fileSize - sent);
        size_t br = fread(buf, 1, want, fp);
        if (br == 0) { ESP_LOGE(TAG, "fread err at %lu", (unsigned long)sent); ok = false; break; }

        int w = esp_http_client_write(client, (const char*)buf, (int)br);
        if (w <= 0) {
            ESP_LOGE(TAG, "write err at %lu: w=%d errno=%d", (unsigned long)sent, w, errno);
            ok = false; break;
        }
        sent += w;
        mProgress.bytesSent = sent;
        notifyProgress();

        if (sent % (50 * 1024) < 1024) {
            ESP_LOGI(TAG, "%s: %luKB/%luKB", filename,
                     (unsigned long)(sent/1024), (unsigned long)(fileSize/1024));
        }
    }

    if (ok) {
        int clen = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "%s: HTTP %d (clen=%d) → %s", filename, status, clen,
                 (status >= 200 && status < 300) ? "OK" : "FAIL");
        ok = (status >= 200 && status < 300);
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
    snprintf(url, sizeof(url), "https://%s:%d/upload/%s/%s/status",
             CONFIG_UPLOAD_SERVER_HOST, CONFIG_UPLOAD_SERVER_PORT,
             deviceId, filename);

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

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
