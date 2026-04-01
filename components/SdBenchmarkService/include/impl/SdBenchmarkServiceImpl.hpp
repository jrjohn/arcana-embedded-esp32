#pragma once

#include "SdBenchmarkService.hpp"

namespace Arcana::SdBench {

class SdBenchmarkServiceImpl : public SdBenchmarkService {
public:
    static SdBenchmarkServiceImpl& getInstance();

    SdBenchmarkResult runBenchmark(uint32_t durationMs = 5000) override;

private:
    SdBenchmarkServiceImpl() = default;
};

} // namespace Arcana::SdBench
