#include <gtest/gtest.h>
#include "CommandTypes.hpp"
#include "commands/PingCommand.hpp"
#include "commands/GetDeviceInfoCommand.hpp"
#include "commands/GetMqttStatusCommand.hpp"

using namespace Arcana::Command;

// ── Helper to build a CommandRequest with sensible defaults ─────────────────
static CommandRequest makeReq(Cluster cluster, uint8_t cmd, CommandSource src = CommandSource::Internal) {
    CommandRequest req;
    req.Source = src;
    req.ConnectionId = 1;
    req.ClusterId = cluster;
    req.Command = cmd;
    req.PayloadLen = 0;
    return req;
}

// ── PingCommand ─────────────────────────────────────────────────────────────

TEST(CommandsTest, PingCommandReturnsOk) {
    PingCommand cmd;
    auto rsp = cmd.Execute(makeReq(Cluster::System, SystemCmd::Ping));
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.ClusterId, Cluster::System);
    EXPECT_EQ(rsp.Command, SystemCmd::Ping);
    EXPECT_EQ(rsp.PayloadLen, sizeof(int64_t));
}

TEST(CommandsTest, PingCommandPropagatesSourceAndConnId) {
    PingCommand cmd;
    auto req = makeReq(Cluster::System, SystemCmd::Ping, CommandSource::BLE);
    req.ConnectionId = 42;
    auto rsp = cmd.Execute(req);
    EXPECT_EQ(rsp.Source, CommandSource::BLE);
    EXPECT_EQ(rsp.ConnectionId, 42);
}

TEST(CommandsTest, PingCommandIsNotAsync) {
    PingCommand cmd;
    EXPECT_FALSE(cmd.IsAsync());
}

// ── GetDeviceInfoCommand ────────────────────────────────────────────────────

TEST(CommandsTest, GetDeviceInfoReturnsOk) {
    GetDeviceInfoCommand cmd;
    auto rsp = cmd.Execute(makeReq(Cluster::System, SystemCmd::GetDeviceInfo));
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.ClusterId, Cluster::System);
    EXPECT_EQ(rsp.Command, SystemCmd::GetDeviceInfo);
    EXPECT_GT(rsp.PayloadLen, 0u);
}

TEST(CommandsTest, GetDeviceInfoPayloadLayout) {
    GetDeviceInfoCommand cmd;
    auto rsp = cmd.Execute(makeReq(Cluster::System, SystemCmd::GetDeviceInfo));

    // Layout: [verLen:1][verStr:N][mac:6][freeHeap:4]
    uint8_t verLen = rsp.Payload[0];
    EXPECT_GT(verLen, 0);
    EXPECT_LT(verLen, 32);  // sane upper bound
    EXPECT_EQ(rsp.PayloadLen, 1u + verLen + 6 + 4);
}

// ── GetMqttStatusCommand ────────────────────────────────────────────────────

TEST(CommandsTest, GetMqttStatusDefaultIsDisconnected) {
    GetMqttStatusCommand cmd;
    auto rsp = cmd.Execute(makeReq(Cluster::Mqtt, MqttCmd::GetStatus));
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.PayloadLen, 1u);
    EXPECT_EQ(rsp.Payload[0], 0);  // not connected
}

TEST(CommandsTest, GetMqttStatusReportsConnected) {
    GetMqttStatusCommand cmd;
    cmd.SetConnected(true);
    auto rsp = cmd.Execute(makeReq(Cluster::Mqtt, MqttCmd::GetStatus));
    EXPECT_EQ(rsp.Payload[0], 1);
}

TEST(CommandsTest, GetMqttStatusToggleConnected) {
    GetMqttStatusCommand cmd;
    cmd.SetConnected(true);
    cmd.SetConnected(false);
    auto rsp = cmd.Execute(makeReq(Cluster::Mqtt, MqttCmd::GetStatus));
    EXPECT_EQ(rsp.Payload[0], 0);
}

// ── CommandRequest / CommandResponse defaults ───────────────────────────────

TEST(CommandsTest, CommandRequestDefaults) {
    CommandRequest req;
    EXPECT_EQ(req.Source, CommandSource::Internal);
    EXPECT_EQ(req.ConnectionId, 0);
    EXPECT_EQ(req.ClusterId, Cluster::System);
    EXPECT_EQ(req.Command, 0);
    EXPECT_EQ(req.PayloadLen, 0);
    EXPECT_EQ(req.StreamId, 0);
    EXPECT_TRUE(req.Fin);
}

TEST(CommandsTest, CommandResponseDefaults) {
    CommandResponse rsp;
    EXPECT_EQ(rsp.Source, CommandSource::Internal);
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.PayloadLen, 0);
    EXPECT_TRUE(rsp.Fin);
}

TEST(CommandsTest, StatusCodeConstants) {
    // Lock down the status code values (wire protocol stability)
    EXPECT_EQ(kStatusOk, 0x00);
    EXPECT_EQ(kStatusUnknownCommand, 0x01);
    EXPECT_EQ(kStatusInvalidParam, 0x02);
    EXPECT_EQ(kStatusBusy, 0x03);
    EXPECT_EQ(kStatusError, 0xFF);
}

TEST(CommandsTest, ClusterEnumValues) {
    // Lock down cluster IDs (wire protocol stability)
    EXPECT_EQ(static_cast<uint8_t>(Cluster::System),   0x00);
    EXPECT_EQ(static_cast<uint8_t>(Cluster::Sensor),   0x01);
    EXPECT_EQ(static_cast<uint8_t>(Cluster::Ble),      0x02);
    EXPECT_EQ(static_cast<uint8_t>(Cluster::Mqtt),     0x03);
    EXPECT_EQ(static_cast<uint8_t>(Cluster::Security), 0x04);
}
