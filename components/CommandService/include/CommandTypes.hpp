#pragma once

#include <cstdint>
#include <cstring>

namespace Arcana {
namespace Command {

enum class CommandSource : uint8_t {
    BLE = 0,
    MQTT,
    Internal
};

enum class FuncCode : uint8_t {
    Ping              = 0x01,
    GetSensorData     = 0x02,
    GetDeviceInfo     = 0x03,
    SetNotifyInterval = 0x04,
    GetBleStatus      = 0x05,
    SetDeviceName     = 0x06,
    BleScan           = 0x07,
    GetMqttStatus     = 0x08,
};

static constexpr uint16_t kMaxRequestPayload  = 128;
static constexpr uint16_t kMaxResponsePayload = 256;

struct CommandRequest {
    CommandSource Source;
    uint16_t ConnectionId;       // BLE conn_id (unused for MQTT)
    FuncCode Function;
    uint8_t Payload[kMaxRequestPayload];
    uint16_t PayloadLen;

    CommandRequest()
        : Source(CommandSource::Internal)
        , ConnectionId(0)
        , Function(FuncCode::Ping)
        , PayloadLen(0) {
        memset(Payload, 0, sizeof(Payload));
    }
};

struct CommandResponse {
    CommandSource Source;
    uint16_t ConnectionId;
    FuncCode Function;
    uint8_t Status;              // 0=OK, non-zero=error code
    uint8_t Payload[kMaxResponsePayload];
    uint16_t PayloadLen;

    CommandResponse()
        : Source(CommandSource::Internal)
        , ConnectionId(0)
        , Function(FuncCode::Ping)
        , Status(0)
        , PayloadLen(0) {
        memset(Payload, 0, sizeof(Payload));
    }
};

// Status codes
static constexpr uint8_t kStatusOk             = 0x00;
static constexpr uint8_t kStatusUnknownCommand = 0x01;
static constexpr uint8_t kStatusInvalidParam   = 0x02;
static constexpr uint8_t kStatusBusy           = 0x03;
static constexpr uint8_t kStatusError          = 0xFF;

} // namespace Command
} // namespace Arcana
