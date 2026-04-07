#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <utility>
#include "MainView.hpp"
#include "LcdViewModel.hpp"
#include "Ssd1306.hpp"

// Test instrumentation: real Ssd1306 ctor takes (gpio_num_t, gpio_num_t, uint8_t).
// Tests/mocks/Ssd1306_stub.cpp provides a no-op implementation that records
// call counts in g_ssdCounters (defined in the stub).
struct Ssd1306Counters {
    int clearCount = 0;
    int displayCount = 0;
    int drawCount = 0;
    std::vector<std::pair<int, std::string>> drawnStrings;
};
extern Ssd1306Counters g_ssdCounters;

using namespace Arcana::Lcd;

// Helper: fresh display + counters
static Ssd1306 makeDisplay() {
    g_ssdCounters = Ssd1306Counters{};
    return Ssd1306{(gpio_num_t)0, (gpio_num_t)0, 0x3C};
}

// ── onEnter draws the static layout ─────────────────────────────────────────

TEST(MainViewTest, OnEnterClearsAndDrawsHeader) {
    MainView view;
    auto display = makeDisplay();

    view.onEnter(display);

    EXPECT_GE(g_ssdCounters.clearCount, 1);
    EXPECT_GE(g_ssdCounters.displayCount, 1);
    EXPECT_GE(g_ssdCounters.drawnStrings.size(), 5u);

    bool foundHeader = false;
    for (auto& s : g_ssdCounters.drawnStrings) {
        if (s.first == 0 && s.second.find("Arcana") != std::string::npos) {
            foundHeader = true;
            break;
        }
    }
    EXPECT_TRUE(foundHeader);
}

// ── render() with sensor data ───────────────────────────────────────────────

TEST(MainViewTest, RenderSensorDirtyDrawsTempAndHumi) {
    MainView view;
    auto display = makeDisplay();
    LcdOutput output;
    output.temperature = 25.5f;
    output.humidity = 60.0f;
    output.dirty = DIRTY_SENSOR;

    view.render(display, output);

    bool foundTemp = false, foundHumi = false;
    for (auto& s : g_ssdCounters.drawnStrings) {
        if (s.second.find("Temp") != std::string::npos)  foundTemp = true;
        if (s.second.find("Humi") != std::string::npos)  foundHumi = true;
    }
    EXPECT_TRUE(foundTemp);
    EXPECT_TRUE(foundHumi);
    EXPECT_EQ(output.dirty, 0);
}

// ── render() with storage stats ─────────────────────────────────────────────

TEST(MainViewTest, RenderStorageDirtyDrawsRecordsAndRate) {
    MainView view;
    auto display = makeDisplay();
    LcdOutput output;
    output.records = 12345;
    output.rate = 100;
    output.dirty = DIRTY_STORAGE;

    view.render(display, output);

    bool foundRec = false, foundRate = false;
    for (auto& s : g_ssdCounters.drawnStrings) {
        if (s.second.find("Records") != std::string::npos) foundRec = true;
        if (s.second.find("Rate")    != std::string::npos) foundRate = true;
    }
    EXPECT_TRUE(foundRec);
    EXPECT_TRUE(foundRate);
}

// ── Toast overlay rendering ─────────────────────────────────────────────────

TEST(MainViewTest, ToastRendersFullScreenWhenActive) {
    MainView view;
    auto display = makeDisplay();
    LcdOutput output;
    strncpy(output.toastMsg, "Uploading...", sizeof(output.toastMsg) - 1);
    output.toastExpiry = 0xFFFFFFFF;
    output.dirty = DIRTY_TOAST;

    view.render(display, output);

    EXPECT_GE(g_ssdCounters.clearCount, 1);
    bool foundToast = false;
    for (auto& s : g_ssdCounters.drawnStrings) {
        if (s.second.find("Uploading") != std::string::npos) {
            foundToast = true;
            break;
        }
    }
    EXPECT_TRUE(foundToast);
}

// ── No dirty flags → render is essentially a no-op ──────────────────────────

TEST(MainViewTest, RenderWithNoDirtyClearsDirtyAfter) {
    MainView view;
    auto display = makeDisplay();
    LcdOutput output;
    output.dirty = 0;

    view.render(display, output);
    EXPECT_EQ(output.dirty, 0);
}

// ── onExit clears the display ───────────────────────────────────────────────

TEST(MainViewTest, OnExitClearsDisplay) {
    MainView view;
    auto display = makeDisplay();
    view.onEnter(display);
    int clearsBefore = g_ssdCounters.clearCount;

    view.onExit(display);
    EXPECT_GT(g_ssdCounters.clearCount, clearsBefore);
}

// ── Combined sensor + storage update ────────────────────────────────────────

TEST(MainViewTest, CombinedSensorAndStorageUpdate) {
    MainView view;
    auto display = makeDisplay();
    LcdOutput output;
    output.temperature = 22.0f;
    output.humidity = 55.0f;
    output.records = 999;
    output.rate = 50;
    output.dirty = DIRTY_SENSOR | DIRTY_STORAGE;

    view.render(display, output);

    EXPECT_EQ(output.dirty, 0);
    EXPECT_GE(g_ssdCounters.drawnStrings.size(), 4u);
}
