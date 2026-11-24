#pragma once

/*
 * Freeze guard value types + pure logic (host-tested in Tests/test_freeze_types.cpp).
 *
 * A task that subscribes to the task watchdog (TWDT) records a "stage" number each time it
 * feeds. The stage survives the panic reboot in RTC no-init memory, so the next boot prints
 * one line saying where the stuck task was: "LC_FREEZE reset=task_wdt stage=2 uptime=812".
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Arcana::Freeze {

/// Main-loop stages. Products add their own values above 100.
enum class Stage : uint32_t {
    Boot = 0,
    LoopWait = 1,
    LoopWork = 2,
    Paused = 3,    // inside an explicit FreezeGuard pause (known-slow call)
};

enum class ResetKind : uint8_t { PowerOn, Software, TaskWdt, IntWdt, OtherWdt, Panic, Brownout, Other };

constexpr uint32_t kBreadcrumbMagic = 0xF2EE2E01u;

inline bool isFreeze(ResetKind k) {
    return k == ResetKind::TaskWdt || k == ResetKind::IntWdt || k == ResetKind::OtherWdt ||
           k == ResetKind::Panic;
}

inline const char* resetKindName(ResetKind k) {
    switch (k) {
    case ResetKind::PowerOn:  return "power_on";
    case ResetKind::Software: return "software";
    case ResetKind::TaskWdt:  return "task_wdt";
    case ResetKind::IntWdt:   return "int_wdt";
    case ResetKind::OtherWdt: return "other_wdt";
    case ResetKind::Panic:    return "panic";
    case ResetKind::Brownout: return "brownout";
    default:                  return "other";
    }
}

/// "LC_FREEZE reset=<kind> stage=<n> uptime=<s>" or, when the breadcrumb is not valid,
/// "LC_FREEZE reset=<kind> stage=unknown". Returns false if not a freeze or buffer too small.
inline bool formatFreezeLine(ResetKind kind, bool crumbValid, uint32_t stage, uint32_t uptimeS,
                             char* out, size_t cap) {
    if (!isFreeze(kind) || !out || cap == 0) return false;
    const int n = crumbValid
        ? std::snprintf(out, cap, "LC_FREEZE reset=%s stage=%lu uptime=%lu", resetKindName(kind),
                        static_cast<unsigned long>(stage), static_cast<unsigned long>(uptimeS))
        : std::snprintf(out, cap, "LC_FREEZE reset=%s stage=unknown", resetKindName(kind));
    return n > 0 && static_cast<size_t>(n) < cap;
}

} // namespace Arcana::Freeze
