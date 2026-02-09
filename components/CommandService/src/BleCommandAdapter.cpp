#include "BleCommandAdapter.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "BleCmdAdapter";

namespace Arcana {
namespace Command {

bool BleCommandAdapter::ParseRequest(uint16_t connId, const uint8_t* data,
                                      uint16_t len, CommandRequest& out) {
    // Minimum: [FuncCode:1][PayloadLen:2]
    if (!data || len < 3) {
        ESP_LOGW(TAG, "Request too short: %u bytes", len);
        return false;
    }

    out.Source = CommandSource::BLE;
    out.ConnectionId = connId;
    out.Function = static_cast<FuncCode>(data[0]);

    uint16_t payloadLen = data[1] | (data[2] << 8);

    if (payloadLen > kMaxRequestPayload) {
        ESP_LOGW(TAG, "Payload too large: %u", payloadLen);
        return false;
    }

    if (len < 3u + payloadLen) {
        ESP_LOGW(TAG, "Incomplete payload: need %u, got %u", 3u + payloadLen, len);
        return false;
    }

    out.PayloadLen = payloadLen;
    if (payloadLen > 0) {
        memcpy(out.Payload, data + 3, payloadLen);
    }

    return true;
}

bool BleCommandAdapter::SerializeResponse(const CommandResponse& rsp,
                                           uint8_t* buf, uint16_t bufSize, uint16_t& outLen) {
    // [FuncCode:1][Status:1][PayloadLen:2 LE][Payload:N]
    uint16_t needed = 4 + rsp.PayloadLen;
    if (bufSize < needed) {
        ESP_LOGW(TAG, "Buffer too small: need %u, have %u", needed, bufSize);
        return false;
    }

    buf[0] = static_cast<uint8_t>(rsp.Function);
    buf[1] = rsp.Status;
    buf[2] = static_cast<uint8_t>(rsp.PayloadLen & 0xFF);
    buf[3] = static_cast<uint8_t>((rsp.PayloadLen >> 8) & 0xFF);

    if (rsp.PayloadLen > 0) {
        memcpy(buf + 4, rsp.Payload, rsp.PayloadLen);
    }

    outLen = needed;
    return true;
}

} // namespace Command
} // namespace Arcana
