#pragma once

#include "AtsStorageService.hpp"
#include "esp_err.h"
#include <cstdint>

namespace Arcana::SdBench {

struct SdBenchmarkResult {
    uint32_t speedKBps10 = 0;   ///< KB/s × 10
    uint32_t totalKB = 0;
    uint32_t totalRecords = 0;
    uint32_t recordsPerSec = 0;
    bool error = false;
};

class SdBenchmarkService {
public:
    struct Input {
        Storage::AtsStorageService* Storage = nullptr;
    };
    Input input;

    virtual ~SdBenchmarkService() = default;

    /// Run sequential write benchmark. Returns result.
    virtual SdBenchmarkResult runBenchmark(uint32_t durationMs = 5000) = 0;

protected:
    SdBenchmarkService() = default;
};

} // namespace Arcana::SdBench
