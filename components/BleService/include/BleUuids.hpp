#pragma once

#include <cstdint>
#include "esp_bt_defs.h"

namespace Arcana {
namespace Ble {

// Bluetooth SIG Standard UUIDs (16-bit)
static constexpr uint16_t UUID_SVC_ENVIRONMENTAL_SENSING = 0x181A;
static constexpr uint16_t UUID_CHAR_TEMPERATURE          = 0x2A6E;
static constexpr uint16_t UUID_CHAR_HUMIDITY             = 0x2A6F;

// Custom UUID (vendor-specific)
static constexpr uint16_t UUID_CHAR_SENSOR_STATUS        = 0xFF01;

// GATT Descriptors
static constexpr uint16_t UUID_DESC_CCCD                 = 0x2902;

// Primary/Characteristic declaration UUIDs
static constexpr uint16_t UUID_PRIMARY_SERVICE           = 0x2800;
static constexpr uint16_t UUID_CHAR_DECLARE              = 0x2803;

} // namespace Ble
} // namespace Arcana
