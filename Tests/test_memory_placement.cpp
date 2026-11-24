// Host tests for core/memory: PSRAM-first placement policy + internal-RAM boot verdict.
#include <gtest/gtest.h>
#include <cstring>
#include "MemoryPlacement.hpp"
#include "MemoryBudget.hpp"

using namespace Arcana::Memory;

namespace {
alignas(8) char gExt[64];
alignas(8) char gInt[64];
int gExtCalls = 0;
int gIntCalls = 0;
void* extOk(size_t)   { ++gExtCalls; return gExt; }
void* extFull(size_t) { ++gExtCalls; return nullptr; }
void* intOk(size_t)   { ++gIntCalls; return gInt; }
void* intFull(size_t) { ++gIntCalls; return nullptr; }
struct Reset { Reset() { gExtCalls = gIntCalls = 0; } };
} // namespace

TEST(MemoryPlacement, PrefersPsramAndNeverTouchesInternalWhenItFits) {
    Reset r;
    Placement p = psramFirst(32, extOk, intOk);
    EXPECT_EQ(p.ptr, gExt);
    EXPECT_EQ(p.where, Where::Psram);
    EXPECT_EQ(gIntCalls, 0);
}

TEST(MemoryPlacement, FallsBackToInternalWithoutPsram) {
    Reset r;
    Placement p = psramFirst(32, extFull, intOk);
    EXPECT_EQ(p.ptr, gInt);
    EXPECT_EQ(p.where, Where::Internal);
    EXPECT_EQ(gExtCalls, 1);
}

TEST(MemoryPlacement, ReportsNoneWhenBothFail) {
    Reset r;
    Placement p = psramFirst(32, extFull, intFull);
    EXPECT_EQ(p.ptr, nullptr);
    EXPECT_EQ(p.where, Where::None);
}

TEST(MemoryPlacement, ZeroBytesAllocatesNothing) {
    Reset r;
    Placement p = psramFirst(0, extOk, intOk);
    EXPECT_EQ(p.ptr, nullptr);
    EXPECT_EQ(gExtCalls + gIntCalls, 0);
}

TEST(MemoryPlacement, WhereNames) {
    EXPECT_STREQ(whereName(Where::Psram), "psram");
    EXPECT_STREQ(whereName(Where::Internal), "internal");
    EXPECT_STREQ(whereName(Where::None), "none");
}

TEST(MemoryBudget, OkAboveThresholdWithLargeBlock) {
    InternalRamSnapshot s{69083, 31744, 8191440};
    EXPECT_EQ(judgeInternalRam(s, 40960), RamVerdict::Ok);
}

TEST(MemoryBudget, LowBelowThreshold) {
    // iNAPLink 0.3.0: 6.9 KB free at boot, BLE + Wi-Fi failed to allocate.
    InternalRamSnapshot s{6900, 4096, 8000000};
    EXPECT_EQ(judgeInternalRam(s, 40960), RamVerdict::Low);
}

TEST(MemoryBudget, LowWhenFragmentedEvenIfTotalIsEnough) {
    InternalRamSnapshot s{60000, 8000, 0};   // 8000 < 40960/4
    EXPECT_EQ(judgeInternalRam(s, 40960), RamVerdict::Low);
}

TEST(MemoryBudget, ThresholdIsExclusive) {
    InternalRamSnapshot s{40960, 10240, 0};
    EXPECT_EQ(judgeInternalRam(s, 40960), RamVerdict::Ok);
}

TEST(MemoryBudget, FormatsTheLineFlashSafeParses) {
    char buf[96];
    InternalRamSnapshot s{69083, 31744, 8191440};
    ASSERT_GT(formatMemLine(s, buf, sizeof buf), 0u);
    EXPECT_STREQ(buf, "MEM internal_free=69083 largest=31744 psram_free=8191440");
}

TEST(MemoryBudget, FormatRefusesTooSmallBuffer) {
    char buf[8];
    InternalRamSnapshot s{1, 2, 3};
    EXPECT_EQ(formatMemLine(s, buf, sizeof buf), 0u);
    EXPECT_EQ(formatMemLine(s, nullptr, 10), 0u);
}
