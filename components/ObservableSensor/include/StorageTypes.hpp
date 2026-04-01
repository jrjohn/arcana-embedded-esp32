#pragma once

#include <cstdint>

namespace Arcana::Storage {

struct StorageStats {
    uint32_t recordCount = 0;
    uint16_t writesPerSec = 0;
    uint32_t totalKB = 0;
    uint16_t kbPerSec = 0;
};

} // namespace Arcana::Storage
