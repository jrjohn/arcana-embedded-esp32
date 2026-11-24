#include "impl/DiagnosticServiceImpl.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_flash.h"
#include "freertos/task.h"
#include "MemoryBudget.hpp"
#include "sdkconfig.h"
#include <cstdio>

#ifndef CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES
#define CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES 40960
#endif

static const char* TAG = "DiagnosticSvc";

namespace Arcana {
namespace Diagnostic {

static constexpr uint8_t kLogIntervalTicks = 10; // 10 × BaseTimer(1s) = 10s

DiagnosticService& DiagnosticServiceImpl::getInstance() {
    static DiagnosticServiceImpl sInstance;
    return sInstance;
}

esp_err_t DiagnosticServiceImpl::init_HAL() {
    return ESP_OK; // No hardware to initialize
}

esp_err_t DiagnosticServiceImpl::init() {
    if (!input.TimerEvents) {
        ESP_LOGE(TAG, "TimerEvents not wired");
        return ESP_ERR_INVALID_STATE;
    }

    input.TimerEvents->Subscribe([this](const Timer::TimerTick&) {
        if (!mRunning.load()) return;

        if (++mTickCount < kLogIntervalTicks) return;
        mTickCount = 0;

        // RAM
        size_t heapFree = esp_get_free_heap_size();
        size_t heapMin  = esp_get_minimum_free_heap_size();
        size_t intFree  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t intTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

        // Flash
        uint32_t flashSize = 0;
        esp_flash_get_size(nullptr, &flashSize);
        const esp_partition_t* app = esp_ota_get_running_partition();
        size_t appSize = app ? app->size : 0;

        ESP_LOGI(TAG,
            "Heap free=%zu min=%zu | DRAM %zu/%zu (%zu%%) | "
            "Flash chip=%luKB app_part=%zuKB | Tasks=%lu",
            heapFree, heapMin,
            intFree, intTotal, intTotal > 0 ? (intFree * 100 / intTotal) : 0,
            (unsigned long)(flashSize / 1024), appSize / 1024,
            (unsigned long)uxTaskGetNumberOfTasks());
    });

    ESP_LOGI(TAG, "Initialized (interval=%ds)", kLogIntervalTicks);
    return ESP_OK;
}

esp_err_t DiagnosticServiceImpl::start() {
    mRunning.store(true);
    mTickCount = 0;
    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void DiagnosticServiceImpl::stop() {
    mRunning.store(false);
    ESP_LOGI(TAG, "Stopped");
}

void DiagnosticServiceImpl::logBootMemory() {
    constexpr uint32_t kInt = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    Memory::InternalRamSnapshot snap;
    snap.freeBytes = static_cast<uint32_t>(heap_caps_get_free_size(kInt));
    snap.largestBlock = static_cast<uint32_t>(heap_caps_get_largest_free_block(kInt));
    snap.psramFree = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    char line[96];
    if (Memory::formatMemLine(snap, line, sizeof line) > 0) {
        printf("%s\n", line);   // plain line, parsed by tools/flash-safe.py
    }
    if (Memory::judgeInternalRam(snap, CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES) ==
        Memory::RamVerdict::Low) {
        ESP_LOGW(TAG, "LOW_INTERNAL_RAM free=%lu largest=%lu threshold=%d — BLE/Wi-Fi/TLS "
                      "allocations may fail; move CPU-only buffers to PSRAM (AGENTS.md §6)",
                 (unsigned long)snap.freeBytes, (unsigned long)snap.largestBlock,
                 CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES);
    }
}

} // namespace Diagnostic
} // namespace Arcana
