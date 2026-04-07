#include <gtest/gtest.h>
#include "LcdViewModel.hpp"

using namespace Arcana::Lcd;
using namespace Arcana;

// ── Initial state ───────────────────────────────────────────────────────────

TEST(LcdViewModelTest, InitialOutputIsZero) {
    LcdViewModel vm;
    auto& out = vm.output();
    EXPECT_FLOAT_EQ(out.temperature, 0.0f);
    EXPECT_FLOAT_EQ(out.humidity, 0.0f);
    EXPECT_EQ(out.records, 0u);
    EXPECT_EQ(out.rate, 0);
    EXPECT_EQ(out.uptimeSec, 0u);
    EXPECT_EQ(out.dirty, DIRTY_ALL);
    EXPECT_EQ(out.toastMsg[0], '\0');
    EXPECT_EQ(out.toastExpiry, 0u);
}

// ── Sensor data subscription ────────────────────────────────────────────────

TEST(LcdViewModelTest, SensorDataUpdatesViewModel) {
    Observable<Sensor::SensorData> sensorObs;
    LcdViewModel vm;
    vm.input.SensorData = &sensorObs;
    vm.init();

    Sensor::SensorData d;
    d.Temperature = 25.5f;
    d.Humidity = 60.0f;
    sensorObs.Notify(d);

    EXPECT_FLOAT_EQ(vm.output().temperature, 25.5f);
    EXPECT_FLOAT_EQ(vm.output().humidity, 60.0f);
    EXPECT_NE(vm.output().dirty & DIRTY_SENSOR, 0);
}

// ── Storage stats subscription ──────────────────────────────────────────────

TEST(LcdViewModelTest, StorageStatsUpdatesViewModel) {
    Observable<Storage::StorageStats> storObs;
    LcdViewModel vm;
    vm.input.StorageStats = &storObs;
    vm.init();

    Storage::StorageStats s;
    s.recordCount = 12345;
    s.writesPerSec = 100;
    storObs.Notify(s);

    EXPECT_EQ(vm.output().records, 12345u);
    EXPECT_EQ(vm.output().rate, 100);
    EXPECT_NE(vm.output().dirty & DIRTY_STORAGE, 0);
}

// ── Timer tick subscription ─────────────────────────────────────────────────

TEST(LcdViewModelTest, TimerTickConvertsToSeconds) {
    Observable<Timer::TimerTick> timerObs;
    LcdViewModel vm;
    vm.input.BaseTimer = &timerObs;
    vm.init();

    Timer::TimerTick t;
    t.Timestamp = 5'000'000;  // 5 seconds in microseconds
    timerObs.Notify(t);

    EXPECT_EQ(vm.output().uptimeSec, 5u);
    EXPECT_NE(vm.output().dirty & DIRTY_TIME, 0);
}

// ── Toast ───────────────────────────────────────────────────────────────────

TEST(LcdViewModelTest, ShowToastSetsMessageAndDirtyFlag) {
    LcdViewModel vm;
    vm.showToast("Hello", 5000);
    EXPECT_STREQ(vm.output().toastMsg, "Hello");
    EXPECT_NE(vm.output().toastExpiry, 0u);
    EXPECT_NE(vm.output().dirty & DIRTY_TOAST, 0);
}

TEST(LcdViewModelTest, ShowToastIndefiniteUsesMaxExpiry) {
    LcdViewModel vm;
    vm.showToast("Forever", 0);
    EXPECT_EQ(vm.output().toastExpiry, 0xFFFFFFFFu);
}

TEST(LcdViewModelTest, ShowToastTruncatesLongMessage) {
    LcdViewModel vm;
    const char* longMsg = "This is a really long toast message that exceeds the buffer";
    vm.showToast(longMsg, 1000);
    EXPECT_LT(strlen(vm.output().toastMsg), 22u);  // null-terminated within buffer
}

TEST(LcdViewModelTest, DismissToastClearsMessage) {
    LcdViewModel vm;
    vm.showToast("Bye", 5000);
    vm.dismissToast();
    EXPECT_EQ(vm.output().toastMsg[0], '\0');
    EXPECT_EQ(vm.output().toastExpiry, 0u);
    EXPECT_NE(vm.output().dirty & DIRTY_ALL, 0);  // forces full redraw
}

// ── Multiple subscriptions interaction ──────────────────────────────────────

TEST(LcdViewModelTest, AllInputsCombineDirtyFlags) {
    Observable<Sensor::SensorData> sensorObs;
    Observable<Storage::StorageStats> storObs;
    Observable<Timer::TimerTick> timerObs;
    LcdViewModel vm;
    vm.input.SensorData = &sensorObs;
    vm.input.StorageStats = &storObs;
    vm.input.BaseTimer = &timerObs;
    vm.init();

    sensorObs.Notify(Sensor::SensorData{});
    storObs.Notify(Storage::StorageStats{});
    timerObs.Notify(Timer::TimerTick{});

    auto& out = vm.output();
    EXPECT_NE(out.dirty & DIRTY_SENSOR, 0);
    EXPECT_NE(out.dirty & DIRTY_STORAGE, 0);
    EXPECT_NE(out.dirty & DIRTY_TIME, 0);
}

TEST(LcdViewModelTest, NoInputWiringIsSafe) {
    LcdViewModel vm;
    // input.SensorData/StorageStats/BaseTimer all nullptr
    vm.init();  // should not crash
    SUCCEED();
}

// ── notifyRender path: init() with non-null render task ───────────────────

TEST(LcdViewModelTest, NotifyRenderHitsXTaskNotifyGiveWhenTaskSet) {
    Observable<Sensor::SensorData> sensorObs;
    LcdViewModel vm;
    vm.input.SensorData = &sensorObs;
    // Provide a non-null fake task handle so notifyRender() reaches
    // xTaskNotifyGive (covered by esp_stubs).
    vm.init(reinterpret_cast<TaskHandle_t>(0xDEADBEEF));

    Sensor::SensorData d;
    d.Temperature = 22.0f;
    sensorObs.Notify(d);
    SUCCEED();
}

// ── DirtyFlag enum ──────────────────────────────────────────────────────────

TEST(LcdViewModelTest, DirtyFlagValuesAreUnique) {
    EXPECT_EQ(int(DIRTY_SENSOR),  0x01);
    EXPECT_EQ(int(DIRTY_STORAGE), 0x02);
    EXPECT_EQ(int(DIRTY_TIME),    0x04);
    EXPECT_EQ(int(DIRTY_TOAST),   0x08);
    EXPECT_EQ(int(DIRTY_ALL),     0xFF);
}
