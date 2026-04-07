#include <gtest/gtest.h>
#include "Log.hpp"
#include "EventCodes.hpp"
#include <vector>

// NOTE: AtsAppender.hpp pulls in ArcanaTsDb.hpp + the entire storage stack.
// AtsAppender coverage will come naturally from test_arcana_ts_db (Phase C)
// where the real ArcanaTsDb is linked.

using namespace arcana::log;

// ── Fake appender that records all events ──────────────────────────────────
//
// IMPORTANT: Logger has no removeAppender() — once added, the pointer is
// retained for the singleton's lifetime. So all RecordingAppenders MUST have
// static storage duration. We register a fixed set of 3 appenders ONCE in
// SetUpTestSuite and clear their event lists between tests.

class RecordingAppender : public IAppender {
public:
    void setMinLevel(Level lvl) { mMinLevel = lvl; }
    void clear() { events.clear(); }
    void append(const LogEvent& event) override { events.push_back(event); }
    Level minLevel() const override { return mMinLevel; }
    std::vector<LogEvent> events;
private:
    Level mMinLevel = Level::Trace;
};

static RecordingAppender g_appA;  // primary, level set per-test
static RecordingAppender g_appB;  // for multi-appender tests
static RecordingAppender g_appC;  // for multi-appender tests

static uint32_t fake_time = 1700000000;
static uint32_t fake_tick = 12345;
static int crit_enter_count = 0;
static int crit_exit_count = 0;

static void fake_enter() { crit_enter_count++; }
static void fake_exit() { crit_exit_count++; }
static uint32_t fake_get_time() { return fake_time; }
static uint32_t fake_get_tick() { return fake_tick; }

static int isr_enter_calls = 0;
static int isr_exit_calls = 0;
static uint32_t isr_mask_passed = 0;
static uint32_t fake_enter_isr() { isr_enter_calls++; return 0xDEAD; }
static void fake_exit_isr(uint32_t mask) { isr_exit_calls++; isr_mask_passed = mask; }

class LoggerFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Register the 3 static appenders ONCE for the entire test suite.
        auto& lg = Logger::getInstance();
        LogConfig cfg{};
        cfg.enterCritical    = fake_enter;
        cfg.exitCritical     = fake_exit;
        cfg.enterCriticalISR = fake_enter_isr;
        cfg.exitCriticalISR  = fake_exit_isr;
        cfg.getTime          = fake_get_time;
        cfg.getTick          = fake_get_tick;
        lg.init(cfg);
        lg.addAppender(&g_appA);
        lg.addAppender(&g_appB);
        lg.addAppender(&g_appC);
    }

    void SetUp() override {
        auto& lg = Logger::getInstance();
        // Drain any leftover events from prior tests
        while (lg.drain(8) > 0) {}
        lg.setLevel(Level::Trace);
        g_appA.clear();
        g_appB.clear();
        g_appC.clear();
        // Default all to Trace; tests override as needed
        g_appA.setMinLevel(Level::Trace);
        g_appB.setMinLevel(Level::Trace);
        g_appC.setMinLevel(Level::Trace);
        crit_enter_count = 0;
        crit_exit_count = 0;
        isr_enter_calls = 0;
        isr_exit_calls = 0;
        isr_mask_passed = 0;
    }
};

// ── Singleton + level ──────────────────────────────────────────────────────

TEST_F(LoggerFixture, GetInstanceReturnsSameSingleton) {
    EXPECT_EQ(&Logger::getInstance(), &Logger::getInstance());
}

TEST_F(LoggerFixture, SetLevelGetLevelRoundTrip) {
    auto& lg = Logger::getInstance();
    lg.setLevel(Level::Warn);
    EXPECT_EQ(lg.getLevel(), Level::Warn);
    lg.setLevel(Level::Trace);
    EXPECT_EQ(lg.getLevel(), Level::Trace);
}

// ── log() → enqueue → drain → appender ─────────────────────────────────────

TEST_F(LoggerFixture, LogEnqueuesEventDeliveredOnDrain) {
    auto& lg = Logger::getInstance();
    lg.log(Level::Info, arcana::ats::ErrorSource::System,
           arcana::evt::SYS_BOOT_OK, 0xCAFEBABE);

    EXPECT_EQ(crit_enter_count, 1);
    EXPECT_EQ(crit_exit_count, 1);
    EXPECT_EQ(lg.pending(), 1);

    uint8_t drained = lg.drain(8);
    EXPECT_EQ(drained, 1);
    ASSERT_EQ(g_appA.events.size(), 1u);
    EXPECT_EQ(g_appA.events[0].code, arcana::evt::SYS_BOOT_OK);
    EXPECT_EQ(g_appA.events[0].param, 0xCAFEBABEu);
    EXPECT_EQ(g_appA.events[0].level, static_cast<uint8_t>(Level::Info));
    EXPECT_EQ(g_appA.events[0].timestamp, fake_time);
}

TEST_F(LoggerFixture, LogFromIsrUsesIsrCriticalSection) {
    auto& lg = Logger::getInstance();
    lg.logFromISR(Level::Error, arcana::ats::ErrorSource::System, 0xBEEF, 42);

    EXPECT_EQ(isr_enter_calls, 1);
    EXPECT_EQ(isr_exit_calls, 1);
    EXPECT_EQ(isr_mask_passed, 0xDEADu);
    EXPECT_EQ(lg.pending(), 1);
}

// ── Appender level filtering ───────────────────────────────────────────────

TEST_F(LoggerFixture, AppenderMinLevelFiltersBelowThreshold) {
    auto& lg = Logger::getInstance();
    g_appA.setMinLevel(Level::Warn);
    g_appB.setMinLevel(Level::Fatal);  // shut B off — only sees Fatal
    g_appC.setMinLevel(Level::Fatal);  // shut C off

    lg.log(Level::Trace, arcana::ats::ErrorSource::System, 0x01);
    lg.log(Level::Info,  arcana::ats::ErrorSource::System, 0x02);
    lg.log(Level::Warn,  arcana::ats::ErrorSource::System, 0x03);
    lg.log(Level::Error, arcana::ats::ErrorSource::System, 0x04);
    lg.drain(8);

    // Only Warn + Error survive on appender A
    ASSERT_EQ(g_appA.events.size(), 2u);
    EXPECT_EQ(g_appA.events[0].code, 0x03);
    EXPECT_EQ(g_appA.events[1].code, 0x04);
    EXPECT_EQ(g_appB.events.size(), 0u);
    EXPECT_EQ(g_appC.events.size(), 0u);
}

// ── Multiple appenders ──────────────────────────────────────────────────────

TEST_F(LoggerFixture, MultipleAppendersAllReceiveQualifyingEvents) {
    auto& lg = Logger::getInstance();
    g_appA.setMinLevel(Level::Trace);
    g_appB.setMinLevel(Level::Info);
    g_appC.setMinLevel(Level::Error);

    lg.log(Level::Debug, arcana::ats::ErrorSource::System, 0xAA);
    lg.log(Level::Error, arcana::ats::ErrorSource::System, 0xBB);
    lg.drain(8);

    EXPECT_EQ(g_appA.events.size(), 2u);  // Trace appender: both events
    EXPECT_EQ(g_appB.events.size(), 1u);  // Info appender: only Error
    EXPECT_EQ(g_appC.events.size(), 1u);  // Error appender: only Error
}

TEST_F(LoggerFixture, AddNullAppenderIsIgnored) {
    auto& lg = Logger::getInstance();
    lg.addAppender(nullptr);  // no crash, no advance
    SUCCEED();
}

// ── Drain semantics ─────────────────────────────────────────────────────────

TEST_F(LoggerFixture, DrainMaxLimitsEventsProcessed) {
    auto& lg = Logger::getInstance();
    g_appB.setMinLevel(Level::Fatal);
    g_appC.setMinLevel(Level::Fatal);

    for (int i = 0; i < 10; i++) {
        lg.log(Level::Info, arcana::ats::ErrorSource::System,
               static_cast<uint16_t>(i));
    }

    uint8_t drained = lg.drain(3);
    EXPECT_EQ(drained, 3);
    EXPECT_EQ(g_appA.events.size(), 3u);

    drained = lg.drain(8);
    EXPECT_EQ(drained, 7);
    EXPECT_EQ(g_appA.events.size(), 10u);
}

TEST_F(LoggerFixture, DrainEmptyReturnsZero) {
    auto& lg = Logger::getInstance();
    EXPECT_EQ(lg.drain(8), 0u);
}

// ── Ring buffer overflow drops events ──────────────────────────────────────

TEST_F(LoggerFixture, RingBufferOverflowDropsExcess) {
    auto& lg = Logger::getInstance();
    // Ring is 32 slots; one is always wasted (head==tail means empty), so
    // capacity is 31. Push 50 events; only 31 should land in the ring.
    for (int i = 0; i < 50; i++) {
        lg.log(Level::Info, arcana::ats::ErrorSource::System,
               static_cast<uint16_t>(i));
    }
    EXPECT_LE(lg.pending(), 31);
    EXPECT_GE(lg.pending(), 30);

    while (lg.drain(8) > 0) {}
    EXPECT_EQ(lg.pending(), 0);
}

// ── Macros ──────────────────────────────────────────────────────────────────

TEST_F(LoggerFixture, MacroDispatchesAtCorrectLevel) {
    auto& lg = Logger::getInstance();
    g_appB.setMinLevel(Level::Fatal);
    g_appC.setMinLevel(Level::Fatal);

    LOG_I(arcana::ats::ErrorSource::System, arcana::evt::SYS_BOOT_OK);
    LOG_W(arcana::ats::ErrorSource::System, arcana::evt::SYS_HEAP_LOW, 1024);
    LOG_E(arcana::ats::ErrorSource::System, arcana::evt::SYS_ASSERT_FAIL);
    lg.drain(8);

    ASSERT_GE(g_appA.events.size(), 3u);
    bool sawInfo = false, sawWarn = false, sawError = false;
    for (auto& e : g_appA.events) {
        if (e.level == static_cast<uint8_t>(Level::Info)) sawInfo = true;
        if (e.level == static_cast<uint8_t>(Level::Warn)) sawWarn = true;
        if (e.level == static_cast<uint8_t>(Level::Error)) sawError = true;
    }
    EXPECT_TRUE(sawInfo);
    EXPECT_TRUE(sawWarn);
    EXPECT_TRUE(sawError);
}

TEST_F(LoggerFixture, MacroSkipsBelowGlobalLevel) {
    auto& lg = Logger::getInstance();
    g_appB.setMinLevel(Level::Fatal);
    g_appC.setMinLevel(Level::Fatal);
    lg.setLevel(Level::Error);  // only Error+Fatal pass

    LOG_T(arcana::ats::ErrorSource::System, 0x01);
    LOG_D(arcana::ats::ErrorSource::System, 0x02);
    LOG_I(arcana::ats::ErrorSource::System, 0x03);
    LOG_W(arcana::ats::ErrorSource::System, 0x04);
    LOG_E(arcana::ats::ErrorSource::System, 0x05);
    LOG_F(arcana::ats::ErrorSource::System, 0x06);
    lg.drain(8);

    EXPECT_EQ(g_appA.events.size(), 2u);
}

// ── EventCodes constants ────────────────────────────────────────────────────

TEST(EventCodesTest, ReservedCodesHaveStableValues) {
    EXPECT_EQ(arcana::evt::SYS_BOOT_OK, 0x0000);
    EXPECT_EQ(arcana::evt::SDIO_INIT_OK, 0x0100);
    EXPECT_EQ(arcana::evt::SENS_READ_OK, 0x0200);
    EXPECT_EQ(arcana::evt::WIFI_CONNECTED, 0x0300);
    EXPECT_EQ(arcana::evt::CRYPTO_KEY_DERIVED, 0x0500);
    EXPECT_EQ(arcana::evt::ATS_DB_OPEN_OK, 0x0600);
    EXPECT_EQ(arcana::evt::MQTT_CONNECTED, 0x0800);
    EXPECT_EQ(arcana::evt::BLE_AT_OK, 0x0900);
    EXPECT_EQ(arcana::evt::OTA_START, 0x0A00);
    EXPECT_EQ(arcana::evt::CMD_DISPATCH, 0x0B00);
    EXPECT_EQ(arcana::evt::UPL_NOT_READY, 0x0C00);
    EXPECT_EQ(arcana::evt::REG_LOADED_CH2, 0x0D00);
}
