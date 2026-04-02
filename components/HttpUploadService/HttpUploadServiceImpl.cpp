#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/tcp.h"
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
// Upload single file — raw TCP socket (like STM32 transparent mode)
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

    uint32_t resumeOffset = queryServerOffset(filename, deviceId);
    if (resumeOffset >= fileSize) {
        ESP_LOGI(TAG, "%s already uploaded", filename);
        fclose(fp); return true;
    }
    if (resumeOffset > 0) {
        ESP_LOGI(TAG, "Resuming from %lu/%lu", (unsigned long)resumeOffset, (unsigned long)fileSize);
        fseek(fp, resumeOffset, SEEK_SET);
    }

    uint32_t remainSize = fileSize - resumeOffset;
    mProgress.bytesSent = resumeOffset;

    // --- Raw TCP connect (like STM32 AT+CIPSTART) ---
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", CONFIG_UPLOAD_SERVER_PORT);

    if (getaddrinfo(CONFIG_UPLOAD_SERVER_HOST, portStr, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "DNS failed: %s", CONFIG_UPLOAD_SERVER_HOST);
        fclose(fp); return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket failed");
        freeaddrinfo(res); fclose(fp); return false;
    }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));  // also for recv
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "TCP connect failed");
        close(sock); freeaddrinfo(res); fclose(fp); return false;
    }
    freeaddrinfo(res);
    ESP_LOGI(TAG, "TCP connected");

    // --- Build raw HTTP header (like STM32) ---
    char header[448];
    uint32_t rangeEnd = resumeOffset + remainSize - 1;
    int hLen = snprintf(header, sizeof(header),
        "POST /upload/%s/%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Length: %lu\r\n"
        "Content-Range: bytes %lu-%lu/%lu\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n"
        "\r\n",
        deviceId, filename, CONFIG_UPLOAD_SERVER_HOST,
        token,
        (unsigned long)remainSize,
        (unsigned long)resumeOffset, (unsigned long)rangeEnd,
        (unsigned long)fileSize);

    ESP_LOGI(TAG, "POST %s (%lu bytes, offset=%lu)", filename,
             (unsigned long)remainSize, (unsigned long)resumeOffset);

    if (send(sock, header, hLen, 0) != hLen) {
        ESP_LOGE(TAG, "Header send failed");
        close(sock); fclose(fp); return false;
    }

    // --- Stream file body ---
    uint8_t buf[CHUNK_SIZE];
    uint32_t sent = 0;
    bool ok = true;
    uint32_t lastLogTick = xTaskGetTickCount();

    ESP_LOGI(TAG, "Starting body stream: %lu bytes", (unsigned long)remainSize);

    while (sent < remainSize) {
        size_t chunkLen = (remainSize - sent > CHUNK_SIZE) ? CHUNK_SIZE : (remainSize - sent);
        size_t br = fread(buf, 1, chunkLen, fp);
        if (sent == 0) {
            ESP_LOGI(TAG, "fread: %u bytes (first chunk)", (unsigned)br);
        }
        if (br == 0) {
            ESP_LOGE(TAG, "Read error at %lu", (unsigned long)(resumeOffset + sent));
            ok = false; break;
        }

        // Send with retry on EAGAIN (TCP buffer full — wait and retry, max 60s)
        {
            size_t toSend = br;
            size_t bufOff = 0;
            int retryCount = 0;
            while (toSend > 0) {
                int w = send(sock, buf + bufOff, toSend, 0);
                if (w > 0) {
                    bufOff += w;
                    toSend -= w;
                    retryCount = 0;
                } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    retryCount++;
                    if (retryCount > 200) {  // 200 × 10ms + SO_SNDTIMEO waits ≈ 60s+
                        ESP_LOGE(TAG, "Send stalled: %d retries at %lu",
                                 retryCount, (unsigned long)(resumeOffset + sent + bufOff));
                        ok = false;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                } else {
                    ESP_LOGE(TAG, "Send failed at %lu (errno=%d)",
                             (unsigned long)(resumeOffset + sent + bufOff), errno);
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
            sent += br;
        }
        if (sent <= CHUNK_SIZE) {
            ESP_LOGI(TAG, "send: first chunk OK, sent=%lu", (unsigned long)sent);
        }
        mProgress.bytesSent = resumeOffset + sent;

        // Progress every chunk (notify LCD) + log every 5 seconds
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
        tv.tv_sec = 10;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char resp[256];
        int rLen = recv(sock, resp, sizeof(resp) - 1, 0);
        if (rLen > 0) {
            resp[rLen] = '\0';
            // Parse HTTP status code from status line: "HTTP/1.x NNN ..."
            const char* sp = strchr(resp, ' ');
            int code = sp ? atoi(sp + 1) : 0;
            ok = (code == 200 || code == 206);
            ESP_LOGI(TAG, "%s: HTTP %d → %s", filename, code, ok ? "OK" : "FAILED");
        } else {
            ESP_LOGW(TAG, "%s: no response (recv=%d, errno=%d)",
                     filename, rLen, errno);
            ok = false;  // cannot confirm success
        }
    }

    close(sock);
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
