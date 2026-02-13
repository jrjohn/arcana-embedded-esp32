#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "esp_crc.h"
#include "esp_log.h"

namespace Arcana {
namespace Command {

class FrameCodec {
public:
    static constexpr uint8_t  kMagic[2] = {0xAC, 0xDA};
    static constexpr uint8_t  kVersion  = 0x01;
    static constexpr size_t   kHeaderLen = 7;   // magic(2) + ver(1) + flags(1) + sid(1) + len(2)
    static constexpr size_t   kCrcLen    = 2;
    static constexpr size_t   kOverhead  = kHeaderLen + kCrcLen; // 9
    static constexpr size_t   kMaxPayloadLen = 300; // Max encrypted response: 277 (pb) + 12 (crypto) = 289

    // Flags
    static constexpr uint8_t  kFlagFin  = 0x01; // Bit 0: last frame in stream

    // Stream ID ranges
    static constexpr uint8_t  kSidNone       = 0x00; // One-shot (no stream)
    static constexpr uint8_t  kSidClientMin  = 0x01; // Client-initiated range start
    static constexpr uint8_t  kSidClientMax  = 0x7F; // Client-initiated range end
    static constexpr uint8_t  kSidServerMin  = 0x80; // Server-initiated range start
    static constexpr uint8_t  kSidServerMax  = 0xFE; // Server-initiated range end
    static constexpr uint8_t  kSidReserved   = 0xFF; // Reserved

    // Frame: payload -> [magic:2][ver:1][flags:1][sid:1][len:2 LE][payload:N][crc:2 LE]
    static bool Frame(const uint8_t* payload, size_t payloadLen,
                      uint8_t* out, size_t outBufSize, size_t& outLen,
                      uint8_t flags = kFlagFin, uint8_t streamId = kSidNone) {
        const size_t totalLen = kHeaderLen + payloadLen + kCrcLen;
        if (totalLen > outBufSize) {
            ESP_LOGW(kTag, "Frame buffer too small: need %zu, have %zu", totalLen, outBufSize);
            return false;
        }

        // Header
        out[0] = kMagic[0];
        out[1] = kMagic[1];
        out[2] = kVersion;
        out[3] = flags;
        out[4] = streamId;
        out[5] = static_cast<uint8_t>(payloadLen & 0xFF);
        out[6] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);

        // Payload
        memcpy(out + kHeaderLen, payload, payloadLen);

        // CRC-16 over magic..payload
        const size_t crcDataLen = kHeaderLen + payloadLen;
        uint16_t crc = esp_crc16_le(0, out, crcDataLen);
        out[crcDataLen]     = static_cast<uint8_t>(crc & 0xFF);
        out[crcDataLen + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

        outLen = totalLen;
        return true;
    }

    // Deframe: [magic:2][ver:1][flags:1][sid:1][len:2 LE][payload:N][crc:2 LE] -> payload ptr + len
    static bool Deframe(const uint8_t* frame, size_t frameLen,
                        const uint8_t*& payload, size_t& payloadLen,
                        uint8_t& flags, uint8_t& streamId) {
        if (frameLen < kOverhead) {
            ESP_LOGW(kTag, "Frame too short: %zu bytes", frameLen);
            return false;
        }

        // Verify magic
        if (frame[0] != kMagic[0] || frame[1] != kMagic[1]) {
            ESP_LOGW(kTag, "Invalid magic: 0x%02x 0x%02x", frame[0], frame[1]);
            return false;
        }

        // Check version
        uint8_t version = frame[2];
        if (version != kVersion) {
            ESP_LOGW(kTag, "Unsupported version: %u", version);
            return false;
        }

        // Read flags + stream ID
        flags = frame[3];
        streamId = frame[4];

        // Read length (LE uint16)
        uint16_t len = static_cast<uint16_t>(frame[5]) |
                       (static_cast<uint16_t>(frame[6]) << 8);

        // Reject zero-length payload
        if (len == 0) {
            ESP_LOGW(kTag, "Zero-length payload");
            return false;
        }

        // Reject oversized payload
        if (len > kMaxPayloadLen) {
            ESP_LOGW(kTag, "Payload exceeds max: %u > %zu", len, kMaxPayloadLen);
            return false;
        }

        // Validate total frame size
        const size_t expectedLen = kHeaderLen + len + kCrcLen;
        if (frameLen < expectedLen) {
            ESP_LOGW(kTag, "Frame length mismatch: expected %zu, got %zu", expectedLen, frameLen);
            return false;
        }

        // Verify CRC-16
        const size_t crcDataLen = kHeaderLen + len;
        uint16_t expectedCrc = esp_crc16_le(0, frame, crcDataLen);
        uint16_t receivedCrc = static_cast<uint16_t>(frame[crcDataLen]) |
                               (static_cast<uint16_t>(frame[crcDataLen + 1]) << 8);
        if (receivedCrc != expectedCrc) {
            ESP_LOGW(kTag, "CRC mismatch: expected 0x%04x, got 0x%04x", expectedCrc, receivedCrc);
            return false;
        }

        payload = frame + kHeaderLen;
        payloadLen = len;
        return true;
    }

private:
    static constexpr const char* kTag = "FrameCodec";
};

} // namespace Command
} // namespace Arcana
