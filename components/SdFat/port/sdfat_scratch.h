#pragma once
// Arcana: where SdFat's two large scratch buffers live (exFAT dual-FAT reseed chunk, 16 KB;
// fragmented-file tail ring, ~1 KB). Selected by Kconfig SDFAT_SCRATCH_IN_PSRAM
// (components/SdFat/Kconfig), which defines SDFAT_SCRATCH_IN_PSRAM=1.
//
// Off (default): static arrays in internal .bss, exactly as upstream-vendored.
// On: allocated once on first use, PSRAM first, internal RAM when there is no PSRAM.
// These buffers are CPU-side only: the sdspi path never DMAs from them. sdmmc_read/
// write_sectors() sees a non-DMA-capable buffer and copies through a temporary DMA buffer
// it allocates from internal RAM per call (largest first, falling back down to 512 B;
// IDF v6.1.1 sdmmc_cmd.c). So static internal RAM drops by ~17 KB, at the cost of a
// transient internal allocation and memcpy during the reseed at mount.
#if SDFAT_SCRATCH_IN_PSRAM
#include <stddef.h>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
inline void* sdfatScratchAlloc(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#else
#include <stdlib.h>
inline void* sdfatScratchAlloc(size_t bytes) { return malloc(bytes); }
#endif
#endif  // SDFAT_SCRATCH_IN_PSRAM
