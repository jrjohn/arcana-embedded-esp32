#pragma once

#include "ICommand.hpp"
#include "OtaService.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace Arcana {
namespace Command {

/**
 * Ota::StartUpdate — kick off a firmware update in a background task.
 *
 * Payload (little-endian, total <= kMaxRequestPayload):
 *   uint16  port
 *   uint32  expectedSize    (0 = don't verify)
 *   uint32  expectedCrc32   (0 = don't verify)
 *   uint8   hostLen, host[hostLen]
 *   uint8   pathLen, path[pathLen]
 *
 * Responds kStatusOk immediately ("update started") — the download runs in
 * its own task because a successful update ends in esp_restart(), so a
 * blocking command would never deliver its response. Poll progress with
 * Ota::GetProgress.
 */
class OtaUpdateCommand : public ICommand {
public:
    struct Params {
        char host[64];
        char path[64];
        uint16_t port;
        uint32_t expectedSize;
        uint32_t expectedCrc32;
    };

    explicit OtaUpdateCommand(OtaService* ota) : mOta(ota) {}

    /// Pure payload parser — host-testable without FreeRTOS.
    static bool ParsePayload(const uint8_t* payload, uint16_t len, Params& out) {
        // fixed part: port(2) + size(4) + crc(4) + hostLen(1) -> >= 11
        if (!payload || len < 12) return false;

        out.port = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
        memcpy(&out.expectedSize, payload + 2, 4);
        memcpy(&out.expectedCrc32, payload + 6, 4);

        uint16_t pos = 10;
        uint8_t hostLen = payload[pos++];
        if (hostLen == 0 || hostLen >= sizeof(out.host)) return false;
        if (pos + hostLen + 1 > len) return false;
        memcpy(out.host, payload + pos, hostLen);
        out.host[hostLen] = '\0';
        pos += hostLen;

        uint8_t pathLen = payload[pos++];
        if (pathLen == 0 || pathLen >= sizeof(out.path)) return false;
        if (pos + pathLen > len) return false;
        memcpy(out.path, payload + pos, pathLen);
        out.path[pathLen] = '\0';

        if (out.port == 0) return false;
        return true;
    }

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = Cluster::Ota;
        rsp.Command = OtaCmd::StartUpdate;

        if (!mOta) {
            rsp.Status = kStatusError;
            return rsp;
        }
        if (mOta->isActive()) {
            rsp.Status = kStatusBusy;
            return rsp;
        }
        if (!ParsePayload(request.Payload, request.PayloadLen, sParams)) {
            rsp.Status = kStatusInvalidParam;
            return rsp;
        }

        sOta = mOta;
        BaseType_t ret = xTaskCreate(otaTask, "ota_update", 6144, nullptr,
                                     tskIDLE_PRIORITY + 1, nullptr);
        if (ret != pdPASS) {
            rsp.Status = kStatusError;
            return rsp;
        }

        rsp.Status = kStatusOk;
        return rsp;
    }

private:
    static void otaTask(void*) {
        sOta->startUpdate(sParams.host, sParams.port, sParams.path,
                          sParams.expectedSize, sParams.expectedCrc32);
        // Only reached on failure — success restarts inside startUpdate()
        vTaskDelete(nullptr);
    }

    // One OTA at a time (guarded by isActive); static storage outlives the
    // command object, which the dispatcher destroys right after Execute().
    static inline Params sParams = {};
    static inline OtaService* sOta = nullptr;

    OtaService* mOta;
};

/**
 * Ota::GetProgress — poll update state.
 * Response payload: uint8 active, uint8 progressPercent.
 */
class GetOtaProgressCommand : public ICommand {
public:
    explicit GetOtaProgressCommand(OtaService* ota) : mOta(ota) {}

    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = Cluster::Ota;
        rsp.Command = OtaCmd::GetProgress;

        if (!mOta) {
            rsp.Status = kStatusError;
            return rsp;
        }

        rsp.Status = kStatusOk;
        rsp.Payload[0] = mOta->isActive() ? 1 : 0;
        rsp.Payload[1] = mOta->getProgress();
        rsp.PayloadLen = 2;
        return rsp;
    }

private:
    OtaService* mOta;
};

} // namespace Command
} // namespace Arcana
