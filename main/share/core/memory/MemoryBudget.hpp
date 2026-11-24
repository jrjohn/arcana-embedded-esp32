#pragma once

/*
 * Internal-RAM budget check printed once at boot, after every service and task is up.
 *
 * The line "MEM internal_free=<n> largest=<n> psram_free=<n>" is what tools/flash-safe.py
 * reads to accept or reject a freshly flashed board. Below CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES
 * (default 40 KB) a warning "LOW_INTERNAL_RAM ..." is logged: at that level Wi-Fi TX bursts,
 * a TLS handshake (~30 KB) or a BLE reconnect can fail to allocate.
 * Pure functions here, so tested on the host (Tests/test_memory_budget.cpp).
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Arcana::Memory {

struct InternalRamSnapshot {
    uint32_t freeBytes = 0;
    uint32_t largestBlock = 0;
    uint32_t psramFree = 0;
};

enum class RamVerdict : uint8_t { Ok, Low };

/// Low when free internal RAM is under the threshold. The largest block matters too:
/// a TLS record buffer or BT controller block needs ~16 KB contiguous, so a heap
/// fragmented below a quarter of the threshold is also Low.
inline RamVerdict judgeInternalRam(const InternalRamSnapshot& s, uint32_t warnBytes) {
    if (s.freeBytes < warnBytes) return RamVerdict::Low;
    if (s.largestBlock < warnBytes / 4) return RamVerdict::Low;
    return RamVerdict::Ok;
}

/// Format the machine-readable boot line (no trailing newline). Returns chars written,
/// or 0 if the buffer is too small.
inline size_t formatMemLine(const InternalRamSnapshot& s, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    const int n = std::snprintf(out, cap, "MEM internal_free=%lu largest=%lu psram_free=%lu",
                                static_cast<unsigned long>(s.freeBytes),
                                static_cast<unsigned long>(s.largestBlock),
                                static_cast<unsigned long>(s.psramFree));
    if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
    return static_cast<size_t>(n);
}

} // namespace Arcana::Memory
