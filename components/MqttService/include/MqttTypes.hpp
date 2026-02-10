#pragma once

#include <cstdint>
#include <cstddef>

namespace Arcana {
namespace Mqtt {

struct MqttCommandEvent {
    const uint8_t* Data;
    size_t Len;
};

struct MqttConnectionStatus {
    bool Connected;
};

} // namespace Mqtt
} // namespace Arcana
