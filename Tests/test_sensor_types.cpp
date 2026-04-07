#include <gtest/gtest.h>
#include "SensorTypes.hpp"

using namespace Arcana::Sensor;

// ── IModel-based types ──────────────────────────────────────────────────────

TEST(SensorTypesTest, SensorDataDefaultValues) {
    SensorData d;
    EXPECT_EQ(d.Value, 0);
    EXPECT_EQ(d.RawValue, 0);
    EXPECT_FLOAT_EQ(d.Temperature, 0.0f);
    EXPECT_FLOAT_EQ(d.Humidity, 0.0f);
    EXPECT_EQ(d.TimestampMs, 0u);
    EXPECT_EQ(d.SensorId, 0);
    EXPECT_EQ(d.Quality, 0);
    EXPECT_EQ(d.GetType(), ModelType::SensorData);
    EXPECT_STREQ(d.GetTypeName(), "SensorData");
}

TEST(SensorTypesTest, SensorDataInterfaceMethods) {
    SensorData d;
    d.SensorId = 7;
    d.TimestampMs = 12345;
    EXPECT_EQ(d.GetSensorId(), 7);
    EXPECT_EQ(d.GetTimestampMs(), 12345u);
}

TEST(SensorTypesTest, SensorErrorConstructWithMessage) {
    SensorError e(-1, "Read failed", 3);
    EXPECT_EQ(e.ErrorCode, -1);
    EXPECT_EQ(e.Message, "Read failed");
    EXPECT_EQ(e.SensorId, 3);
    EXPECT_EQ(e.GetType(), ModelType::SensorError);
    EXPECT_STREQ(e.GetTypeName(), "SensorError");
}

TEST(SensorTypesTest, SensorErrorDefaultConstructor) {
    SensorError e;
    EXPECT_EQ(e.ErrorCode, 0);
    EXPECT_EQ(e.SensorId, 0);
    EXPECT_TRUE(e.Message.empty());
}

TEST(SensorTypesTest, ThresholdEventConstruct) {
    ThresholdEvent t(ThresholdEvent::Type::High, 100, 80, 2);
    EXPECT_EQ(t.EventType, ThresholdEvent::Type::High);
    EXPECT_EQ(t.Value, 100);
    EXPECT_EQ(t.Threshold, 80);
    EXPECT_EQ(t.SensorId, 2);
    EXPECT_EQ(t.GetType(), ModelType::ThresholdEvent);
}

// Cover the IModel accessor overrides for SensorError + ThresholdEvent
TEST(SensorTypesTest, SensorErrorIModelAccessors) {
    SensorError e(-7, "fail", 9);
    e.TimestampMs = 4242;
    EXPECT_EQ(e.GetSensorId(), 9);
    EXPECT_EQ(e.GetTimestampMs(), 4242u);
    EXPECT_STREQ(e.GetTypeName(), "SensorError");
}

TEST(SensorTypesTest, ThresholdEventIModelAccessors) {
    ThresholdEvent t(ThresholdEvent::Type::Low, 5, 10, 4);
    t.TimestampMs = 1111;
    EXPECT_EQ(t.GetSensorId(), 4);
    EXPECT_EQ(t.GetTimestampMs(), 1111u);
    EXPECT_STREQ(t.GetTypeName(), "ThresholdEvent");
}

TEST(SensorTypesTest, LifecycleEventGetTimestampMs) {
    LifecycleEvent l(LifecycleEvent::State::Stopped, 6);
    l.TimestampMs = 8888;
    EXPECT_EQ(l.GetTimestampMs(), 8888u);
}

TEST(SensorTypesTest, LifecycleEventStateNameFallthrough) {
    // Cast an out-of-range value to State to hit the "Unknown" return path
    LifecycleEvent l(static_cast<LifecycleEvent::State>(99), 0);
    EXPECT_STREQ(l.GetStateName(), "Unknown");
}

TEST(SensorTypesTest, LifecycleEventStateName) {
    EXPECT_STREQ(LifecycleEvent(LifecycleEvent::State::Started, 0).GetStateName(),       "Started");
    EXPECT_STREQ(LifecycleEvent(LifecycleEvent::State::Stopped, 0).GetStateName(),       "Stopped");
    EXPECT_STREQ(LifecycleEvent(LifecycleEvent::State::Initialized, 0).GetStateName(),   "Initialized");
    EXPECT_STREQ(LifecycleEvent(LifecycleEvent::State::Deinitialized, 0).GetStateName(), "Deinitialized");
}

TEST(SensorTypesTest, LifecycleEventInterface) {
    LifecycleEvent l(LifecycleEvent::State::Started, 5);
    EXPECT_EQ(l.GetType(), ModelType::LifecycleEvent);
    EXPECT_STREQ(l.GetTypeName(), "LifecycleEvent");
    EXPECT_EQ(l.GetSensorId(), 5);
}

// ── ModelCast / IsModelType ─────────────────────────────────────────────────

TEST(SensorTypesTest, ModelCastSucceedsForCorrectType) {
    SensorData d;
    d.Value = 42;
    const IModel& m = d;
    auto* ptr = ModelCast<SensorData>(m);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->Value, 42);
}

TEST(SensorTypesTest, ModelCastFailsForWrongType) {
    SensorData d;
    const IModel& m = d;
    EXPECT_EQ(ModelCast<SensorError>(m), nullptr);
    EXPECT_EQ(ModelCast<ThresholdEvent>(m), nullptr);
    EXPECT_EQ(ModelCast<LifecycleEvent>(m), nullptr);
}

TEST(SensorTypesTest, IsModelTypeReturnsCorrectly) {
    SensorError e;
    const IModel& m = e;
    EXPECT_TRUE(IsModelType<SensorError>(m));
    EXPECT_FALSE(IsModelType<SensorData>(m));
}

// ── SensorConfig builder ────────────────────────────────────────────────────

TEST(SensorTypesTest, SensorConfigDefaults) {
    SensorConfig c;
    EXPECT_EQ(c.SensorId, 0);
    EXPECT_EQ(c.ReadIntervalMs, 1000u);
    EXPECT_EQ(c.ThresholdHigh, 0);
    EXPECT_EQ(c.ThresholdLow, 0);
    EXPECT_FALSE(c.EnableThresholdEvents);
    EXPECT_EQ(c.TaskStackSize, 4096u);
    EXPECT_EQ(c.TaskPriority, 5);
}

TEST(SensorTypesTest, SensorConfigBuilderChainable) {
    SensorConfig c = SensorConfig{}
        .WithId(3)
        .WithInterval(500)
        .WithThresholds(10, 90)
        .WithStackSize(8192)
        .WithPriority(10);

    EXPECT_EQ(c.SensorId, 3);
    EXPECT_EQ(c.ReadIntervalMs, 500u);
    EXPECT_EQ(c.ThresholdLow, 10);
    EXPECT_EQ(c.ThresholdHigh, 90);
    EXPECT_TRUE(c.EnableThresholdEvents);
    EXPECT_EQ(c.TaskStackSize, 8192u);
    EXPECT_EQ(c.TaskPriority, 10);
}

// ── Variant alternative ─────────────────────────────────────────────────────

TEST(SensorTypesTest, SensorDataVConvertFromIModel) {
    SensorData d;
    d.Value = 42;
    d.Temperature = 25.5f;
    d.SensorId = 1;

    Variant::SensorDataV v(d);
    EXPECT_EQ(v.Value, 42);
    EXPECT_FLOAT_EQ(v.Temperature, 25.5f);
    EXPECT_EQ(v.SensorId, 1);
}

TEST(SensorTypesTest, SensorErrorVTruncatesLongMessage) {
    char longMsg[200];
    memset(longMsg, 'X', sizeof(longMsg) - 1);
    longMsg[sizeof(longMsg) - 1] = '\0';

    Variant::SensorErrorV v(-99, longMsg, 2);
    EXPECT_EQ(v.ErrorCode, -99);
    EXPECT_EQ(v.SensorId, 2);
    EXPECT_LT(strlen(v.Message), sizeof(v.Message));  // null-terminated
}

TEST(SensorTypesTest, ThresholdEventVConvertFromIModel) {
    ThresholdEvent t(ThresholdEvent::Type::Low, 5, 10, 1);
    Variant::ThresholdEventV v(t);
    EXPECT_EQ(v.EventType, Variant::ThresholdEventV::Type::Low);
    EXPECT_EQ(v.Value, 5);
    EXPECT_EQ(v.Threshold, 10);
}

TEST(SensorTypesTest, LifecycleEventVStateName) {
    EXPECT_STREQ(Variant::LifecycleEventV(Variant::LifecycleEventV::State::Started, 0).GetStateName(),       "Started");
    EXPECT_STREQ(Variant::LifecycleEventV(Variant::LifecycleEventV::State::Stopped, 0).GetStateName(),       "Stopped");
    EXPECT_STREQ(Variant::LifecycleEventV(Variant::LifecycleEventV::State::Initialized, 0).GetStateName(),   "Initialized");
    EXPECT_STREQ(Variant::LifecycleEventV(Variant::LifecycleEventV::State::Deinitialized, 0).GetStateName(), "Deinitialized");
}

TEST(SensorTypesTest, LifecycleEventVStateNameFallthrough) {
    // Out-of-range State value hits Variant::LifecycleEventV's "Unknown" fallthrough
    Variant::LifecycleEventV v(static_cast<Variant::LifecycleEventV::State>(99), 0);
    EXPECT_STREQ(v.GetStateName(), "Unknown");
}

// ── SensorEvent variant helpers ─────────────────────────────────────────────

TEST(SensorTypesTest, GetSensorIdFromVariant) {
    SensorEvent e = Variant::SensorDataV{};
    auto& d = std::get<Variant::SensorDataV>(e);
    d.SensorId = 7;
    EXPECT_EQ(GetSensorId(e), 7);
}

TEST(SensorTypesTest, GetTimestampFromVariant) {
    Variant::SensorErrorV err;
    err.TimestampMs = 9999;
    SensorEvent e = err;
    EXPECT_EQ(GetTimestampMs(e), 9999u);
}

TEST(SensorTypesTest, GetTypeNameFromVariant) {
    EXPECT_STREQ(GetTypeName(SensorEvent{Variant::SensorDataV{}}),     "SensorData");
    EXPECT_STREQ(GetTypeName(SensorEvent{Variant::SensorErrorV{}}),    "SensorError");
    EXPECT_STREQ(GetTypeName(SensorEvent{Variant::ThresholdEventV{}}), "ThresholdEvent");
    EXPECT_STREQ(GetTypeName(SensorEvent{Variant::LifecycleEventV{}}), "LifecycleEvent");
}

TEST(SensorTypesTest, ToVariantConvertsAllTypes) {
    SensorData d;       d.Value = 42;
    SensorError er(-1, "test", 0);
    ThresholdEvent t(ThresholdEvent::Type::High, 100, 80, 1);
    LifecycleEvent l(LifecycleEvent::State::Started, 2);

    SensorEvent ev1 = ToVariant(d);
    SensorEvent ev2 = ToVariant(er);
    SensorEvent ev3 = ToVariant(t);
    SensorEvent ev4 = ToVariant(l);

    EXPECT_TRUE(std::holds_alternative<Variant::SensorDataV>(ev1));
    EXPECT_TRUE(std::holds_alternative<Variant::SensorErrorV>(ev2));
    EXPECT_TRUE(std::holds_alternative<Variant::ThresholdEventV>(ev3));
    EXPECT_TRUE(std::holds_alternative<Variant::LifecycleEventV>(ev4));
}

TEST(SensorTypesTest, EventVisitorPatternMatching) {
    SensorEvent e = Variant::SensorDataV{};
    int dataCount = 0, errorCount = 0;
    std::visit(EventVisitor{
        [&](const Variant::SensorDataV&)     { dataCount++; },
        [&](const Variant::SensorErrorV&)    { errorCount++; },
        [&](const Variant::ThresholdEventV&) {},
        [&](const Variant::LifecycleEventV&) {}
    }, e);
    EXPECT_EQ(dataCount, 1);
    EXPECT_EQ(errorCount, 0);
}
