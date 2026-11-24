#pragma once

/*
 * Where big memory goes: PSRAM first, internal RAM only when there is no PSRAM.
 *
 * Internal RAM is the scarce resource. The BLE controller, Wi-Fi, lwIP and every DMA
 * buffer must come from it. A product built from this template (iNAPLink 0.3.0,
 * 2026-11-24) passed host tests and arch-check but booted with 6.9 KB internal RAM
 * free, and BLE + Wi-Fi failed to allocate. Moving CPU-only buffers and task stacks to
 * PSRAM brought static internal DIRAM from 116,681 B to 67,913 B. See AGENTS.md §6.
 *
 * Safe in PSRAM:   buffers only the CPU reads/writes (parsers, rings, canvases, scratch).
 * NEVER in PSRAM:  - buffers handed to SPI/I2S/LCD DMA (spi_master would malloc an internal
 *                    bounce buffer of the same size on every transaction);
 *                  - std::atomic, portMUX_TYPE, FreeRTOS static objects (S32C1I does not
 *                    work on external RAM);
 *                  - the stack of a task that writes flash (NVS, OTA, partitions), directly
 *                    or through a synchronous Observable subscriber: spi_flash asserts
 *                    esp_task_stack_is_sane_cache_disabled();
 *                  - anything an ISR in IRAM touches while the cache is off.
 * Do NOT use EXT_RAM_BSS_ATTR instead: it needs SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY,
 * which also moves the .bss of Wi-Fi / lwIP / BT into PSRAM.
 *
 * The placement policy (psramFirst) is a pure function so it is tested on the host
 * (Tests/test_memory_placement.cpp); the ESP-IDF wrappers below are thin.
 */

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sdkconfig.h"
#endif

namespace Arcana::Memory {

enum class Where : uint8_t { None, Psram, Internal };

struct Placement {
    void* ptr = nullptr;
    Where where = Where::None;
};

/// Pure policy: try the external allocator, then the internal one.
/// `ext` / `in` are callables `void*(size_t)`.
template <typename ExtAlloc, typename IntAlloc>
Placement psramFirst(size_t bytes, ExtAlloc&& ext, IntAlloc&& in) {
    if (bytes == 0) return {};
    if (void* p = ext(bytes)) return {p, Where::Psram};
    if (void* p = in(bytes)) return {p, Where::Internal};
    return {};
}

/// Name for logs ("psram" / "internal" / "none").
inline const char* whereName(Where w) {
    switch (w) {
    case Where::Psram:    return "psram";
    case Where::Internal: return "internal";
    default:              return "none";
    }
}

#ifdef ESP_PLATFORM

/// Large CPU-only buffer, PSRAM first. Free with heap_caps_free(). Never for DMA.
inline Placement allocLarge(size_t bytes) {
    return psramFirst(
        bytes,
        [](size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); },
        [](size_t n) { return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); });
}

/// xTaskCreatePinnedToCore with the stack in PSRAM when the build allows it
/// (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM), internal RAM otherwise. The TCB stays
/// internal. Only for tasks that never write flash (see header comment).
/// A task created on PSRAM must be deleted with vTaskDeleteWithCaps(), never
/// vTaskDelete(nullptr) from inside itself: the caller must keep it running forever
/// or delete it from another task. `where` (optional) reports the stack placement.
inline BaseType_t createTaskPsramStack(TaskFunction_t fn, const char* name,
                                       uint32_t stackBytes, void* arg, UBaseType_t prio,
                                       TaskHandle_t* out, BaseType_t core,
                                       Where* where = nullptr) {
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    if (xTaskCreatePinnedToCoreWithCaps(fn, name, stackBytes, arg, prio, out, core,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        if (where) *where = Where::Psram;
        return pdPASS;
    }
#endif
    const BaseType_t r = xTaskCreatePinnedToCore(fn, name, stackBytes, arg, prio, out, core);
    if (where) *where = (r == pdPASS) ? Where::Internal : Where::None;
    return r;
}

#endif // ESP_PLATFORM

} // namespace Arcana::Memory
