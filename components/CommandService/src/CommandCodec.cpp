#include "CommandCodec.hpp"
#include "arcana_cmd.pb.h"
#include "esp_log.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstring>

static const char* TAG = "CommandCodec";

namespace Arcana {
namespace Command {

#ifdef CONFIG_CMD_ENCRYPTION_ENABLED
static int HexCharToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool HexToKey(const char* hex, uint8_t key[16]) {
    if (strlen(hex) != 32) {
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        int hi = HexCharToNibble(hex[i * 2]);
        int lo = HexCharToNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}
#endif

esp_err_t CommandCodec::Init() {
#ifdef CONFIG_CMD_ENCRYPTION_ENABLED
    mEncryptionEnabled = true;
    ESP_LOGI(TAG, "Encryption enabled");

    uint8_t key[16];
    if (!HexToKey(CONFIG_CMD_ENCRYPTION_PSK, key)) {
        ESP_LOGE(TAG, "Invalid PSK hex string");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = mCrypto.Init(key);
    if (err != ESP_OK) {
        return err;
    }
#else
    mEncryptionEnabled = false;
    ESP_LOGI(TAG, "Encryption disabled");
#endif

    ESP_LOGI(TAG, "Initialized (protobuf + %s)",
             mEncryptionEnabled ? "AES-128-CCM" : "plaintext");
    return ESP_OK;
}

bool CommandCodec::DecodeRequest(CommandSource source, uint16_t connId,
                                  const uint8_t* data, size_t len,
                                  CommandRequest& out) {
    const uint8_t* pbData = data;
    size_t pbLen = len;
    uint8_t plainBuf[arcana_CmdRequest_size];

    // Decrypt if encryption is enabled
    if (mEncryptionEnabled) {
        size_t plainLen = 0;
        if (!mCrypto.Decrypt(data, len, plainBuf, sizeof(plainBuf), plainLen)) {
            ESP_LOGW(TAG, "Decryption failed");
            return false;
        }
        pbData = plainBuf;
        pbLen = plainLen;
    }

    // Decode protobuf
    arcana_CmdRequest msg = arcana_CmdRequest_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(pbData, pbLen);
    if (!pb_decode(&stream, arcana_CmdRequest_fields, &msg)) {
        ESP_LOGW(TAG, "Protobuf decode failed: %s", PB_GET_ERROR(&stream));
        return false;
    }

    // Map to CommandRequest
    out.Source = source;
    out.ConnectionId = connId;
    out.Function = static_cast<FuncCode>(msg.func);
    out.PayloadLen = static_cast<uint16_t>(msg.payload.size);
    if (msg.payload.size > 0 && msg.payload.size <= kMaxRequestPayload) {
        memcpy(out.Payload, msg.payload.bytes, msg.payload.size);
    } else if (msg.payload.size > kMaxRequestPayload) {
        ESP_LOGW(TAG, "Request payload too large: %u", (unsigned)msg.payload.size);
        return false;
    }

    ESP_LOGD(TAG, "Decoded request: func=0x%02x payload=%u bytes",
             msg.func, (unsigned)msg.payload.size);
    return true;
}

bool CommandCodec::EncodeResponse(const CommandResponse& rsp,
                                   uint8_t* buf, size_t bufSize, size_t& outLen) {
    // Map CommandResponse to protobuf struct
    arcana_CmdResponse msg = arcana_CmdResponse_init_zero;
    msg.func = static_cast<uint32_t>(rsp.Function);
    msg.status = rsp.Status;
    msg.payload.size = rsp.PayloadLen;
    if (rsp.PayloadLen > 0) {
        memcpy(msg.payload.bytes, rsp.Payload, rsp.PayloadLen);
    }

    // Encode protobuf
    uint8_t pbBuf[arcana_CmdResponse_size];
    pb_ostream_t stream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
    if (!pb_encode(&stream, arcana_CmdResponse_fields, &msg)) {
        ESP_LOGW(TAG, "Protobuf encode failed: %s", PB_GET_ERROR(&stream));
        return false;
    }
    size_t pbLen = stream.bytes_written;

    // Encrypt if enabled
    if (mEncryptionEnabled) {
        if (!mCrypto.Encrypt(pbBuf, pbLen, buf, bufSize, outLen)) {
            ESP_LOGW(TAG, "Encryption failed");
            return false;
        }
    } else {
        if (bufSize < pbLen) {
            ESP_LOGW(TAG, "Output buffer too small: need %zu, have %zu", pbLen, bufSize);
            return false;
        }
        memcpy(buf, pbBuf, pbLen);
        outLen = pbLen;
    }

    ESP_LOGD(TAG, "Encoded response: func=0x%02x status=%d wire=%zu bytes",
             msg.func, msg.status, outLen);
    return true;
}

} // namespace Command
} // namespace Arcana
