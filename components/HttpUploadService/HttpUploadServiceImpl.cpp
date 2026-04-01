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

    struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

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

    while (sent < remainSize) {
        size_t chunkLen = (remainSize - sent > CHUNK_SIZE) ? CHUNK_SIZE : (remainSize - sent);
        size_t br = fread(buf, 1, chunkLen, fp);
        if (br == 0) {
            ESP_LOGE(TAG, "Read error at %lu", (unsigned long)(resumeOffset + sent));
            ok = false; break;
        }

        // Send with retry on EAGAIN (TCP buffer full — wait and retry)
        {
            size_t toSend = br;
            size_t bufOff = 0;
            while (toSend > 0) {
                int w = send(sock, buf + bufOff, toSend, 0);
                if (w > 0) {
                    bufOff += w;
                    toSend -= w;
                } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
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
        mProgress.bytesSent = resumeOffset + sent;

        // Progress every chunk (notify LCD) + log every 100KB
        notifyProgress();
        if (sent % (CHUNK_SIZE * 50) == 0 || sent >= remainSize) {
            uint8_t pct = (uint8_t)((mProgress.bytesSent * 100ULL) / fileSize);
            ESP_LOGI(TAG, "%s: %u%% (%lu/%lu)", filename, pct,
                     (unsigned long)mProgress.bytesSent, (unsigned long)fileSize);
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
            ok = (strstr(resp, "200") != nullptr);
            ESP_LOGI(TAG, "%s: %s", filename, ok ? "OK (200)" : "FAILED");
        } else {
            ESP_LOGW(TAG, "No response received");
        }
    }

    close(sock);
    fclose(fp);
    return ok;
}

// ---------------------------------------------------------------------------
// Query resume offset (still uses esp_http_client — small GET, no body)
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
