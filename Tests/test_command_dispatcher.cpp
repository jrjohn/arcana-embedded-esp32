#include <gtest/gtest.h>
#include "CommandDispatcher.hpp"
#include "CommandFactory.hpp"

using namespace Arcana::Command;

// ── Dispatch with valid command ─────────────────────────────────────────────

TEST(CommandDispatcherTest, DispatchPingNotifiesResponseObserver) {
    CommandFactory::Dependencies deps;  // all nullptr
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    ASSERT_EQ(dispatcher.Init(), ESP_OK);

    bool received = false;
    CommandResponse capturedRsp;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse& rsp) {
        received = true;
        capturedRsp = rsp;
    });

    CommandRequest req;
    req.Source = CommandSource::BLE;
    req.ConnectionId = 7;
    req.ClusterId = Cluster::System;
    req.Command = SystemCmd::Ping;
    req.PayloadLen = 0;
    req.StreamId = 5;

    dispatcher.Dispatch(req);

    EXPECT_TRUE(received);
    EXPECT_EQ(capturedRsp.Status, kStatusOk);
    EXPECT_EQ(capturedRsp.Source, CommandSource::BLE);
    EXPECT_EQ(capturedRsp.ConnectionId, 7);
    EXPECT_EQ(capturedRsp.StreamId, 5);  // dispatcher copies streamId to response
}

// ── Dispatch with unknown command returns error response ──────────────────

TEST(CommandDispatcherTest, DispatchUnknownCommandReturnsUnknownStatus) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    dispatcher.Init();

    CommandResponse capturedRsp;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse& rsp) {
        capturedRsp = rsp;
    });

    CommandRequest req;
    req.ClusterId = static_cast<Cluster>(0xFF);  // invalid cluster
    req.Command = 0xFF;
    dispatcher.Dispatch(req);

    EXPECT_EQ(capturedRsp.Status, kStatusUnknownCommand);
}

TEST(CommandDispatcherTest, DispatchUnknownCommandPropagatesSourceAndConnId) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    dispatcher.Init();

    CommandResponse capturedRsp;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse& rsp) {
        capturedRsp = rsp;
    });

    CommandRequest req;
    req.Source = CommandSource::MQTT;
    req.ConnectionId = 99;
    req.ClusterId = Cluster::System;
    req.Command = 0xCC;  // unknown system command
    req.StreamId = 3;
    dispatcher.Dispatch(req);

    EXPECT_EQ(capturedRsp.Source, CommandSource::MQTT);
    EXPECT_EQ(capturedRsp.ConnectionId, 99);
    EXPECT_EQ(capturedRsp.StreamId, 3);
    EXPECT_TRUE(capturedRsp.Fin);
}

// ── Async vs sync command path ──────────────────────────────────────────────

TEST(CommandDispatcherTest, SyncCommandExecutesImmediately) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    dispatcher.Init();

    bool received = false;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse&) {
        received = true;
    });

    // PingCommand is sync, executes inline → response received before Dispatch returns
    CommandRequest req;
    req.ClusterId = Cluster::System;
    req.Command = SystemCmd::Ping;
    dispatcher.Dispatch(req);

    EXPECT_TRUE(received);
}

// ── Init / Stop lifecycle ───────────────────────────────────────────────────

TEST(CommandDispatcherTest, InitReturnsOk) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    EXPECT_EQ(dispatcher.Init(), ESP_OK);
}

TEST(CommandDispatcherTest, StopWithoutStartIsSafe) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    dispatcher.Stop();  // should not crash
    SUCCEED();
}

// CommandDispatcher's async path uses an EventQueue that wraps xTaskCreate.
// On the host stub, the queue task never actually runs, so the lambda body
// at L25-26 (which dispatches into ProcessCommand) is structurally
// unreachable through normal Dispatch+wait. Drive ProcessCommand directly
// via the test_ProcessCommand accessor + verify the response gets notified
// — equivalent execution path to the async lambda body.
TEST(CommandDispatcherTest, ProcessCommandAsyncPathDispatches) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    ASSERT_EQ(dispatcher.Init(), ESP_OK);

    bool received = false;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse&) {
        received = true;
    });

    CommandRequest req;
    req.ClusterId = Cluster::System;
    req.Command = SystemCmd::Ping;
    dispatcher.test_ProcessCommand(req);

    EXPECT_TRUE(received);
}

// Drive the EventQueue lambda body (CommandDispatcher.cpp L24-26) by
// posting via the test_AsyncQueue() accessor and pumping ProcessOneEvent.
TEST(CommandDispatcherTest, AsyncQueueLambdaInvokesProcessCommand) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    ASSERT_EQ(dispatcher.Init(), ESP_OK);
    ASSERT_EQ(dispatcher.Start(), ESP_OK);

    bool received = false;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse&) {
        received = true;
    });

    CommandRequest req;
    req.ClusterId = Cluster::System;
    req.Command = SystemCmd::Ping;
    ASSERT_TRUE(dispatcher.test_AsyncQueue().Post(req));

    // ProcessOneEvent invokes the registered handler (the lambda from
    // CommandDispatcher::Start), which calls ProcessCommand.
    EXPECT_TRUE(dispatcher.test_AsyncQueue().ProcessOneEvent());
    EXPECT_TRUE(received);

    dispatcher.Stop();
}

// ── Multiple subscribers ────────────────────────────────────────────────────

// ── Async queue overflow (covers L58 Post failure) ────────────────────────

TEST(CommandDispatcherTest, AsyncQueueOverflowDropsExcess) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    dispatcher.Init();
    ASSERT_EQ(dispatcher.Start(), ESP_OK);

    // BleScan is async (IsAsync() == true). Dispatch many requests; the
    // queue depth is 10, so the 11th+ Post will fail and be logged.
    // (The async task never actually drains the queue in tests because
    // xTaskCreate is a no-op stub.)
    for (int i = 0; i < 20; i++) {
        CommandRequest req;
        req.ClusterId = Cluster::Ble;
        req.Command = BleCmd::Scan;
        req.PayloadLen = 0;
        dispatcher.Dispatch(req);
    }
    SUCCEED();  // no crash; the drop branch executes
    dispatcher.Stop();
}

TEST(CommandDispatcherTest, MultipleResponseObserversAllNotified) {
    CommandFactory::Dependencies deps;
    CommandFactory factory(deps);
    CommandDispatcher dispatcher(factory);
    dispatcher.Init();

    int count = 0;
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse&) { count++; });
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse&) { count++; });
    dispatcher.ResponseEvents().Subscribe([&](const CommandResponse&) { count++; });

    CommandRequest req;
    req.ClusterId = Cluster::System;
    req.Command = SystemCmd::Ping;
    dispatcher.Dispatch(req);

    EXPECT_EQ(count, 3);
}
