#pragma once

#include <cstdint>

namespace Arcana::Storage {

struct StorageStats {
    uint32_t recordCount = 0;       // records in the current day-file (sensor.ats)
    uint16_t writesPerSec = 0;
    uint32_t totalKB = 0;
    uint16_t kbPerSec = 0;
    uint64_t lifetimeRecords = 0;   // grand total across all rotated days + current
};

} // namespace Arcana::Storage
