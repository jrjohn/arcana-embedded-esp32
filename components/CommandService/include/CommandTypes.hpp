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

// ---------------------------------------------------------------------------
// Cluster + Command (Matter/ZCL style two-layer dispatch)
// ---------------------------------------------------------------------------

enum class Cluster : uint8_t {
    System   = 0x00,
    Sensor   = 0x01,
    Ble      = 0x02,
    Mqtt     = 0x03,
    Security = 0x04,
};

namespace SystemCmd {
    static constexpr uint8_t Ping          = 0x01;
    static constexpr uint8_t GetDeviceInfo = 0x02;
}

namespace SensorCmd {
    static constexpr uint8_t GetData           = 0x01;
    static constexpr uint8_t SetNotifyInterval = 0x02;
}

namespace BleCmd {
    static constexpr uint8_t GetStatus     = 0x01;
    static constexpr uint8_t SetDeviceName = 0x02;
    static constexpr uint8_t Scan          = 0x03;
}

namespace MqttCmd {
    static constexpr uint8_t GetStatus = 0x01;
}

namespace SecurityCmd {
    static constexpr uint8_t KeyExchange = 0x01;
}

static constexpr uint16_t kMaxRequestPayload  = 128;
static constexpr uint16_t kMaxResponsePayload = 256;

struct CommandRequest {
    CommandSource Source;
    uint16_t ConnectionId;       // BLE conn_id (unused for MQTT)
    Cluster ClusterId;
    uint8_t Command;
    uint8_t Payload[kMaxRequestPayload];
    uint16_t PayloadLen;

    CommandRequest()
        : Source(CommandSource::Internal)
        , ConnectionId(0)
        , ClusterId(Cluster::System)
        , Command(0)
        , PayloadLen(0) {
        memset(Payload, 0, sizeof(Payload));
    }
};

struct CommandResponse {
    CommandSource Source;
    uint16_t ConnectionId;
    Cluster ClusterId;
    uint8_t Command;
    uint8_t Status;              // 0=OK, non-zero=error code
    uint8_t Payload[kMaxResponsePayload];
    uint16_t PayloadLen;

    CommandResponse()
        : Source(CommandSource::Internal)
        , ConnectionId(0)
        , ClusterId(Cluster::System)
        , Command(0)
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
