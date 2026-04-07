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

// ── Multiple subscribers ────────────────────────────────────────────────────

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
