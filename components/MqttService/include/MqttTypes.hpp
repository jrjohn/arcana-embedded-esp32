#pragma once

#include <cstdint>
#include <cstddef>

namespace Arcana {
namespace Mqtt {

struct MqttCommandEvent {
    static constexpr size_t kMaxDataLen = 298; // Max wire size (response frame)
    uint8_t Data[kMaxDataLen];                 // Value copy (not pointer into MQTT buffer)
    size_t Len;
};

struct MqttConnectionStatus {
    bool Connected;
};

} // namespace Mqtt
} // namespace Arcana
