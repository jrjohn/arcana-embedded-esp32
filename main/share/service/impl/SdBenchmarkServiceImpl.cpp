#include "impl/SdBenchmarkServiceImpl.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "SdBench";
static const char* BENCH_FILE = "/sdcard/bench.tmp";
// 32KB blocks measure the bus ceiling rather than per-call FATFS overhead
// (4KB blocks cost one cluster-chain walk per ~1ms of bus time).
static const size_t BLOCK_SIZE = 32 * 1024;

namespace Arcana::SdBench {

SdBenchmarkServiceImpl& SdBenchmarkServiceImpl::getInstance() {
    static SdBenchmarkServiceImpl sInstance;
    return sInstance;
}

SdBenchmarkResult SdBenchmarkServiceImpl::runBenchmark(uint32_t durationMs) {
    SdBenchmarkResult result;

    FILE* fp = fopen(BENCH_FILE, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot create benchmark file");
        result.error = true;
        return result;
    }

    static uint8_t buf[BLOCK_SIZE];  // static — 32KB would overflow the task stack
    memset(buf, 0xAA, sizeof(buf));

    int64_t startUs = esp_timer_get_time();
    int64_t endUs = startUs + (int64_t)durationMs * 1000;
    uint32_t blocksWritten = 0;

    while (esp_timer_get_time() < endUs) {
        size_t written = fwrite(buf, 1, BLOCK_SIZE, fp);
        if (written != BLOCK_SIZE) {
            ESP_LOGE(TAG, "Write failed at block %lu", (unsigned long)blocksWritten);
            result.error = true;
            break;
        }
        blocksWritten++;

        // Sync every 8 blocks (256KB) to measure real disk speed (not cache)
        if ((blocksWritten & 7) == 0) {
            fflush(fp);
        }
    }

    fflush(fp);
    fclose(fp);

    int64_t elapsedUs = esp_timer_get_time() - startUs;
    uint32_t elapsedMs = (uint32_t)(elapsedUs / 1000);

    result.totalKB = blocksWritten * (BLOCK_SIZE / 1024);
    result.totalRecords = blocksWritten;

    if (elapsedMs > 0) {
        result.speedKBps10 = (uint32_t)(result.totalKB * 10000ULL / elapsedMs);
        result.recordsPerSec = (uint32_t)(blocksWritten * 1000ULL / elapsedMs);
    }

    // Cleanup
    remove(BENCH_FILE);

    ESP_LOGI(TAG, "Benchmark: %lu KB in %lu ms = %lu.%lu KB/s",
             (unsigned long)result.totalKB,
             (unsigned long)elapsedMs,
             (unsigned long)(result.speedKBps10 / 10),
             (unsigned long)(result.speedKBps10 % 10));

    return result;
}

} // namespace Arcana::SdBench
