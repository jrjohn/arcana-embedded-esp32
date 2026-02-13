#include "CommandCodec.hpp"
#include "FrameCodec.hpp"
#include "KeyExchangeManager.hpp"
#include "arcana_cmd.pb.h"
#include "esp_log.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstring>

static const char* TAG = "CommandCodec";

namespace Arcana {
namespace Command {

// Compile-time: ensure FrameCodec payload limit accommodates max encrypted response
static_assert(FrameCodec::kMaxPayloadLen >= arcana_CmdResponse_size + CryptoEngine::kOverhead,
              "kMaxPayloadLen must accommodate max encrypted response");

esp_err_t CommandCodec::Init() {
#ifdef CONFIG_CMD_ENCRYPTION_ENABLED
    mEncryptionEnabled = true;
    ESP_LOGI(TAG, "Encryption enabled");

    uint8_t key[CryptoEngine::kKeyLen];
    if (!CryptoEngine::HexToKey(CONFIG_CMD_ENCRYPTION_PSK, key)) {
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
             mEncryptionEnabled ? "AES-256-CCM" : "plaintext");
    return ESP_OK;
}

bool CommandCodec::DecodeRequest(CommandSource source, uint16_t connId,
                                  const uint8_t* data, size_t len,
                                  CommandRequest& out) {
    // Deframe: strip header + verify CRC
    const uint8_t* framePayload = nullptr;
    size_t framePayloadLen = 0;
    uint8_t flags = 0;
    uint8_t streamId = 0;
    if (!FrameCodec::Deframe(data, len, framePayload, framePayloadLen, flags, streamId)) {
        return false;
    }

    const uint8_t* pbData = framePayload;
    size_t pbLen = framePayloadLen;
    uint8_t plainBuf[arcana_CmdRequest_size];

    // Decrypt if encryption is enabled
    if (mEncryptionEnabled) {
        size_t plainLen = 0;
        bool decrypted = false;

        // Try session key first (mutex-protected, no dangling pointer)
        if (mKeyExchangeMgr) {
            decrypted = mKeyExchangeMgr->DecryptWithSession(
                source, connId, framePayload, framePayloadLen,
                plainBuf, sizeof(plainBuf), plainLen);
        }

        // Fallback to PSK
        if (!decrypted) {
            if (!mCrypto.Decrypt(framePayload, framePayloadLen,
                                 plainBuf, sizeof(plainBuf), plainLen)) {
                ESP_LOGW(TAG, "Decryption failed (session + PSK)");
                return false;
            }
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

    // Validate cluster and command range before narrowing cast
    if (msg.cluster > 0xFF) {
        ESP_LOGW(TAG, "Cluster out of range: 0x%x", (unsigned)msg.cluster);
        return false;
    }
    if (msg.command > 0xFF) {
        ESP_LOGW(TAG, "Command out of range: 0x%x", (unsigned)msg.command);
        return false;
    }

    // Map to CommandRequest
    out.Source = source;
    out.ConnectionId = connId;
    out.ClusterId = static_cast<Cluster>(msg.cluster);
    out.Command = static_cast<uint8_t>(msg.command);
    out.PayloadLen = static_cast<uint16_t>(msg.payload.size);
    out.StreamId = streamId;
    out.Fin = (flags & FrameCodec::kFlagFin) != 0;
    if (msg.payload.size > 0 && msg.payload.size <= kMaxRequestPayload) {
        memcpy(out.Payload, msg.payload.bytes, msg.payload.size);
    } else if (msg.payload.size > kMaxRequestPayload) {
        ESP_LOGW(TAG, "Request payload too large: %u", (unsigned)msg.payload.size);
        return false;
    }

    ESP_LOGD(TAG, "Decoded request: cluster=0x%02x cmd=0x%02x payload=%u bytes",
             msg.cluster, msg.command, (unsigned)msg.payload.size);
    return true;
}

bool CommandCodec::EncodeResponse(const CommandResponse& rsp,
                                   uint8_t* buf, size_t bufSize, size_t& outLen) {
    // Map CommandResponse to protobuf struct
    arcana_CmdResponse msg = arcana_CmdResponse_init_zero;
    msg.cluster = static_cast<uint32_t>(rsp.ClusterId);
    msg.command = static_cast<uint32_t>(rsp.Command);
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

    // Inner buffer for pre-frame payload (encrypted or plaintext)
    // Max: protobuf + crypto overhead
    uint8_t innerBuf[arcana_CmdResponse_size + CryptoEngine::kOverhead];
    size_t innerLen = 0;

    // Encrypt if enabled
    if (mEncryptionEnabled) {
        bool isKeyExchangeOk = (rsp.ClusterId == Cluster::Security &&
                                rsp.Command == SecurityCmd::KeyExchange &&
                                rsp.Status == kStatusOk);

        bool encrypted = false;

        // KeyExchange response ALWAYS uses PSK (session not yet installed)
        // Otherwise try session key first (mutex-protected, no dangling pointer)
        if (!isKeyExchangeOk && mKeyExchangeMgr) {
            encrypted = mKeyExchangeMgr->EncryptWithSession(
                rsp.Source, rsp.ConnectionId,
                pbBuf, pbLen, innerBuf, sizeof(innerBuf), innerLen);
        }

        // PSK fallback
        if (!encrypted) {
            if (!mCrypto.Encrypt(pbBuf, pbLen, innerBuf, sizeof(innerBuf), innerLen)) {
                ESP_LOGW(TAG, "Encryption failed");
                return false;
            }
        }

        // After encrypting a successful KeyExchange response, install the session
        if (isKeyExchangeOk && mKeyExchangeMgr) {
            mKeyExchangeMgr->InstallPendingSession(rsp.Source, rsp.ConnectionId);
        }
    } else {
        memcpy(innerBuf, pbBuf, pbLen);
        innerLen = pbLen;
    }

    // Frame: wrap inner payload with header + CRC
    uint8_t flags = rsp.Fin ? FrameCodec::kFlagFin : 0;
    if (!FrameCodec::Frame(innerBuf, innerLen, buf, bufSize, outLen, flags, rsp.StreamId)) {
        return false;
    }

    ESP_LOGD(TAG, "Encoded response: cluster=0x%02x cmd=0x%02x status=%d wire=%zu bytes",
             msg.cluster, msg.command, msg.status, outLen);
    return true;
}

} // namespace Command
} // namespace Arcana
