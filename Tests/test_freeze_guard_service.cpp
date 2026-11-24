// Host tests for the task-watchdog freeze guard (FreezeTypes + FreezeGuardServiceImpl).
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "FreezeTypes.hpp"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "impl/FreezeGuardServiceImpl.hpp"

using namespace Arcana::Freeze;

// ── stubs owned by this test ───────────────────────────────────────────────
static esp_reset_reason_t gReset = ESP_RST_POWERON;
static int gAdds = 0, gDeletes = 0, gResets = 0;
static esp_err_t gAddResult = ESP_OK;
extern "C" esp_reset_reason_t esp_reset_reason(void) { return gReset; }
extern "C" esp_err_t esp_task_wdt_add(TaskHandle_t) { ++gAdds; return gAddResult; }
extern "C" esp_err_t esp_task_wdt_delete(TaskHandle_t) { ++gDeletes; return ESP_OK; }
extern "C" esp_err_t esp_task_wdt_reset(void) { ++gResets; return ESP_OK; }

namespace {
void resetCounters() { gAdds = gDeletes = gResets = 0; gAddResult = ESP_OK; }
}

TEST(FreezeTypes, OnlyWatchdogAndPanicResetsAreFreezes) {
    EXPECT_TRUE(isFreeze(ResetKind::TaskWdt));
    EXPECT_TRUE(isFreeze(ResetKind::IntWdt));
    EXPECT_TRUE(isFreeze(ResetKind::OtherWdt));
    EXPECT_TRUE(isFreeze(ResetKind::Panic));
    EXPECT_FALSE(isFreeze(ResetKind::PowerOn));
    EXPECT_FALSE(isFreeze(ResetKind::Software));
    EXPECT_FALSE(isFreeze(ResetKind::Brownout));
    EXPECT_FALSE(isFreeze(ResetKind::Other));
}

TEST(FreezeTypes, FormatsStageAndUptime) {
    char buf[96];
    ASSERT_TRUE(formatFreezeLine(ResetKind::TaskWdt, true, 2, 812, buf, sizeof buf));
    EXPECT_STREQ(buf, "LC_FREEZE reset=task_wdt stage=2 uptime=812");
}

TEST(FreezeTypes, UnknownStageWhenBreadcrumbInvalid) {
    char buf[96];
    ASSERT_TRUE(formatFreezeLine(ResetKind::Panic, false, 0, 0, buf, sizeof buf));
    EXPECT_STREQ(buf, "LC_FREEZE reset=panic stage=unknown");
}

TEST(FreezeTypes, NoLineForNormalBootOrTinyBuffer) {
    char buf[8];
    EXPECT_FALSE(formatFreezeLine(ResetKind::PowerOn, true, 1, 1, buf, sizeof buf));
    EXPECT_FALSE(formatFreezeLine(ResetKind::TaskWdt, true, 1, 1, buf, sizeof buf));
    EXPECT_FALSE(formatFreezeLine(ResetKind::TaskWdt, true, 1, 1, nullptr, 0));
}

TEST(FreezeTypes, ResetKindNames) {
    EXPECT_STREQ(resetKindName(ResetKind::PowerOn), "power_on");
    EXPECT_STREQ(resetKindName(ResetKind::Software), "software");
    EXPECT_STREQ(resetKindName(ResetKind::IntWdt), "int_wdt");
    EXPECT_STREQ(resetKindName(ResetKind::OtherWdt), "other_wdt");
    EXPECT_STREQ(resetKindName(ResetKind::Brownout), "brownout");
    EXPECT_STREQ(resetKindName(ResetKind::Other), "other");
}

TEST(FreezeGuardService, WatchFeedPauseResumeDrivesTheWatchdog) {
    resetCounters();
    auto& g = FreezeGuardServiceImpl::getInstance();
    EXPECT_EQ(g.init_HAL(), ESP_OK);
    EXPECT_EQ(g.init(), ESP_OK);
    EXPECT_EQ(g.start(), ESP_OK);
    EXPECT_EQ(g.watchdogTimeoutS(), 10);

    g.feed(Stage::LoopWork);              // not watching yet: no reset
    EXPECT_EQ(gResets, 0);

    EXPECT_EQ(g.watchCurrentTask(), ESP_OK);
    EXPECT_EQ(gAdds, 1);
    g.feed(Stage::LoopWait);
    EXPECT_EQ(gResets, 1);

    g.pause("wifi reconnect");
    EXPECT_EQ(gDeletes, 1);
    g.feed(Stage::LoopWork);              // paused: no reset
    EXPECT_EQ(gResets, 1);
    g.pause("again");                     // idempotent
    EXPECT_EQ(gDeletes, 1);

    g.resume();
    EXPECT_EQ(gAdds, 2);
    g.resume();                           // idempotent
    EXPECT_EQ(gAdds, 2);
    g.feed(Stage::LoopWork);
    EXPECT_EQ(gResets, 2);

    g.stop();
    EXPECT_EQ(gDeletes, 2);
    g.stop();
    EXPECT_EQ(gDeletes, 2);
}

TEST(FreezeGuardService, FailedSubscribeNeverFeeds) {
    resetCounters();
    gAddResult = ESP_ERR_INVALID_STATE;
    auto& g = FreezeGuardServiceImpl::getInstance();
    EXPECT_NE(g.watchCurrentTask(), ESP_OK);
    g.feed(Stage::LoopWork);
    EXPECT_EQ(gResets, 0);
    g.stop();
    EXPECT_EQ(gDeletes, 0);
}

TEST(FreezeGuardService, PrintsFreezeLineAfterWatchdogReset) {
    resetCounters();
    auto& g = FreezeGuardServiceImpl::getInstance();
    gReset = ESP_RST_POWERON;
    g.init_HAL();                         // arms the breadcrumb
    g.watchCurrentTask();
    g.feed(Stage::LoopWork);
    g.stop();

    gReset = ESP_RST_TASK_WDT;            // "reboot" — RTC_NOINIT survives
    testing::internal::CaptureStdout();
    g.init_HAL();
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("LC_FREEZE reset=task_wdt stage=2"), std::string::npos) << out;

    for (esp_reset_reason_t r : {ESP_RST_INT_WDT, ESP_RST_WDT, ESP_RST_PANIC, ESP_RST_SW,
                                 ESP_RST_BROWNOUT, ESP_RST_EXT}) {
        gReset = r;
        EXPECT_EQ(g.init_HAL(), ESP_OK);
    }
    gReset = ESP_RST_POWERON;
}
