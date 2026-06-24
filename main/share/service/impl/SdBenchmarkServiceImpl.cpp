#include "impl/SdBenchmarkServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "SdBench";
static const char* BENCH_FILE = "bench.tmp";   // volume-relative (SdFat exFAT)
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

    // Write through SdFat's exFAT on the storage service's shared volume, each
    // SD op serialized by the storage SD mutex (SdFat has no internal locking).
    auto& storage = static_cast<Storage::AtsStorageServiceImpl&>(
        Storage::AtsStorageServiceImpl::getInstance());
    arcana::ats::IMutex* sdmtx = storage.sdMutex();
    arcana::ats::ExFatFilePort fp(storage.exfatVolume());

    {
        sdmtx->lock();
        bool ok = fp.open(BENCH_FILE, arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE);
        sdmtx->unlock();
        if (!ok) {
            ESP_LOGE(TAG, "Cannot create benchmark file");
            result.error = true;
            return result;
        }
    }

    static uint8_t buf[BLOCK_SIZE];  // static — 32KB would overflow the task stack
    memset(buf, 0xAA, sizeof(buf));

    int64_t startUs = esp_timer_get_time();
    int64_t endUs = startUs + (int64_t)durationMs * 1000;
    uint32_t blocksWritten = 0;

    while (esp_timer_get_time() < endUs) {
        sdmtx->lock();
        int32_t written = fp.write(buf, BLOCK_SIZE);
        // Sync every 8 blocks (256KB) to measure real disk speed (not cache).
        if (written == (int32_t)BLOCK_SIZE && ((blocksWritten + 1) & 7) == 0) {
            fp.sync();
        }
        sdmtx->unlock();
        if (written != (int32_t)BLOCK_SIZE) {
            ESP_LOGE(TAG, "Write failed at block %lu", (unsigned long)blocksWritten);
            result.error = true;
            break;
        }
        blocksWritten++;
    }

    { sdmtx->lock(); fp.sync(); fp.close(); sdmtx->unlock(); }

    int64_t elapsedUs = esp_timer_get_time() - startUs;
    uint32_t elapsedMs = (uint32_t)(elapsedUs / 1000);

    result.totalKB = blocksWritten * (BLOCK_SIZE / 1024);
    result.totalRecords = blocksWritten;

    if (elapsedMs > 0) {
        result.speedKBps10 = (uint32_t)(result.totalKB * 10000ULL / elapsedMs);
        result.recordsPerSec = (uint32_t)(blocksWritten * 1000ULL / elapsedMs);
    }

    // Cleanup
    { sdmtx->lock(); storage.exfatVolume()->remove("/bench.tmp"); sdmtx->unlock(); }

    ESP_LOGI(TAG, "Benchmark: %lu KB in %lu ms = %lu.%lu KB/s",
             (unsigned long)result.totalKB,
             (unsigned long)elapsedMs,
             (unsigned long)(result.speedKBps10 / 10),
             (unsigned long)(result.speedKBps10 % 10));

    return result;
}

} // namespace Arcana::SdBench
