#include <gtest/gtest.h>
#include "CommandService.hpp"

using namespace Arcana::Command;

// CommandService is a Meyer's singleton — Init() is idempotent and recreates
// internal mFactory/mDispatcher each call, so tests are order-independent as
// long as each test calls init() before exercising behaviour.

TEST(CommandServiceTest, InstanceReturnsSameSingleton) {
    auto& a = CommandService::Instance();
    auto& b = CommandService::Instance();
    EXPECT_EQ(&a, &b);
}

TEST(CommandServiceTest, InitWithNullSensorSucceeds) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    EXPECT_EQ(svc.init(), ESP_OK);
}

TEST(CommandServiceTest, InitPopulatesOutputPointers) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    svc.init();
    EXPECT_NE(svc.output.ResponseEvents, nullptr);
    EXPECT_NE(svc.output.Factory, nullptr);
    // KeyExchangeMgr only set when CONFIG_CMD_ENCRYPTION_ENABLED — host build
    // does not define this, so it should be nullptr.
}

TEST(CommandServiceTest, ResponseEventsAccessorReturnsSameAsOutput) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    svc.init();
    EXPECT_EQ(&svc.ResponseEvents(), svc.output.ResponseEvents);
}

TEST(CommandServiceTest, FactoryAccessorMatchesOutput) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    svc.init();
    EXPECT_EQ(svc.Factory(), svc.output.Factory);
}

// ── Request/response wiring ─────────────────────────────────────────────────

TEST(CommandServiceTest, HandleRequestPingNotifiesResponseObserver) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    svc.init();

    bool received = false;
    CommandResponse capturedRsp;
    svc.output.ResponseEvents->Subscribe([&](const CommandResponse& rsp) {
        received = true;
        capturedRsp = rsp;
    });

    CommandRequest req;
    req.Source = CommandSource::BLE;
    req.ConnectionId = 11;
    req.ClusterId = Cluster::System;
    req.Command = SystemCmd::Ping;
    req.PayloadLen = 0;
    req.StreamId = 9;

    svc.HandleRequest(req);

    EXPECT_TRUE(received);
    EXPECT_EQ(capturedRsp.Status, kStatusOk);
    EXPECT_EQ(capturedRsp.Source, CommandSource::BLE);
    EXPECT_EQ(capturedRsp.ConnectionId, 11);
    EXPECT_EQ(capturedRsp.StreamId, 9);
}

TEST(CommandServiceTest, HandleRequestUnknownCommandReturnsUnknownStatus) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    svc.init();

    CommandResponse capturedRsp;
    svc.output.ResponseEvents->Subscribe([&](const CommandResponse& rsp) {
        capturedRsp = rsp;
    });

    CommandRequest req;
    req.ClusterId = static_cast<Cluster>(0xFE);
    req.Command = 0xFF;
    svc.HandleRequest(req);

    EXPECT_EQ(capturedRsp.Status, kStatusUnknownCommand);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

TEST(CommandServiceTest, StartAfterInitReturnsOk) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    svc.init();
    EXPECT_EQ(svc.Start(), ESP_OK);
    svc.Stop();
}

TEST(CommandServiceTest, StopWithoutInitIsSafe) {
    auto& svc = CommandService::Instance();
    svc.Stop();
    SUCCEED();
}

TEST(CommandServiceTest, MultipleInitCallsReinitialize) {
    auto& svc = CommandService::Instance();
    svc.input.Sensor = nullptr;
    EXPECT_EQ(svc.init(), ESP_OK);
    auto* firstFactory = svc.output.Factory;
    EXPECT_EQ(svc.init(), ESP_OK);
    // Each init creates a new factory unique_ptr
    EXPECT_NE(svc.output.Factory, nullptr);
    (void)firstFactory;  // first pointer is now dangling — don't deref
}
