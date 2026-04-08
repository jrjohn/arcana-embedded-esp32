#pragma once

#include <cstdint>

namespace Arcana {
namespace Timer {

struct TimerTick {
    int64_t Timestamp = 0;  // µs since boot
};

} // namespace Timer
} // namespace Arcana
