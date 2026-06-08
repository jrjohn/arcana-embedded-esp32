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

#include "commands/OtaCommands.hpp"

namespace {
// Recording fake for the OtaService interface
struct FakeOta : Arcana::OtaService {
    bool active = false;
    uint8_t progress = 0;
    int startCalls = 0;
    bool startUpdate(const char*, uint16_t, const char*, uint32_t, uint32_t) override {
        startCalls++; return true;
    }
    uint8_t getProgress() const override { return progress; }
    bool isActive() const override { return active; }
};

// Build a valid StartUpdate payload
uint16_t buildOtaPayload(uint8_t* out, const char* host, const char* path,
                         uint16_t port, uint32_t size, uint32_t crc) {
    uint16_t pos = 0;
    out[pos++] = port & 0xFF; out[pos++] = port >> 8;
    memcpy(out + pos, &size, 4); pos += 4;
    memcpy(out + pos, &crc, 4); pos += 4;
    uint8_t hl = strlen(host); out[pos++] = hl; memcpy(out + pos, host, hl); pos += hl;
    uint8_t pl = strlen(path); out[pos++] = pl; memcpy(out + pos, path, pl); pos += pl;
    return pos;
}
} // namespace

TEST(OtaCommandsTest, ParseValidPayload) {
    uint8_t buf[128];
    uint16_t len = buildOtaPayload(buf, "192.168.11.5", "/fw.bin", 8070, 123456, 0xDEADBEEF);

    OtaUpdateCommand::Params p;
    ASSERT_TRUE(OtaUpdateCommand::ParsePayload(buf, len, p));
    EXPECT_STREQ(p.host, "192.168.11.5");
    EXPECT_STREQ(p.path, "/fw.bin");
    EXPECT_EQ(p.port, 8070);
    EXPECT_EQ(p.expectedSize, 123456u);
    EXPECT_EQ(p.expectedCrc32, 0xDEADBEEF);
}

TEST(OtaCommandsTest, ParseRejectsMalformed) {
    OtaUpdateCommand::Params p;
    uint8_t buf[128];

    EXPECT_FALSE(OtaUpdateCommand::ParsePayload(nullptr, 64, p));
    EXPECT_FALSE(OtaUpdateCommand::ParsePayload(buf, 5, p));            // too short

    uint16_t len = buildOtaPayload(buf, "h", "/p", 0, 0, 0);            // port 0
    EXPECT_FALSE(OtaUpdateCommand::ParsePayload(buf, len, p));

    len = buildOtaPayload(buf, "host", "/path", 80, 0, 0);
    EXPECT_FALSE(OtaUpdateCommand::ParsePayload(buf, len - 3, p));      // truncated path

    buf[10] = 200;                                                      // hostLen overflow
    EXPECT_FALSE(OtaUpdateCommand::ParsePayload(buf, len, p));
}

TEST(OtaCommandsTest, StartRejectsWhenBusyOrUnwired) {
    uint8_t buf[128];
    uint16_t len = buildOtaPayload(buf, "h.local", "/fw.bin", 80, 0, 0);

    CommandRequest req;
    req.ClusterId = Cluster::Ota;
    req.Command = OtaCmd::StartUpdate;
    memcpy(req.Payload, buf, len);
    req.PayloadLen = len;

    OtaUpdateCommand unwired(nullptr);
    EXPECT_EQ(unwired.Execute(req).Status, kStatusError);

    FakeOta ota; ota.active = true;
    OtaUpdateCommand busy(&ota);
    EXPECT_EQ(busy.Execute(req).Status, kStatusBusy);

    CommandRequest bad = req; bad.PayloadLen = 4;
    ota.active = false;
    OtaUpdateCommand cmd(&ota);
    EXPECT_EQ(cmd.Execute(bad).Status, kStatusInvalidParam);
}

TEST(OtaCommandsTest, StartAcceptsValidPayload) {
    uint8_t buf[128];
    uint16_t len = buildOtaPayload(buf, "192.168.11.44", "/mqtt5.bin",
                                   8070, 1688064, 0xf204eea8);
    CommandRequest req;
    req.ClusterId = Cluster::Ota;
    req.Command = OtaCmd::StartUpdate;
    memcpy(req.Payload, buf, len);
    req.PayloadLen = len;

    FakeOta ota;  // not active, wired
    OtaUpdateCommand cmd(&ota);
    auto rsp = cmd.Execute(req);   // host xTaskCreate stub returns pdPASS
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.ClusterId, Cluster::Ota);
    EXPECT_EQ(rsp.Command, OtaCmd::StartUpdate);
}

TEST(OtaCommandsTest, GetProgressReportsState) {
    FakeOta ota; ota.active = true; ota.progress = 42;
    GetOtaProgressCommand cmd(&ota);

    CommandRequest req;
    req.ClusterId = Cluster::Ota;
    req.Command = OtaCmd::GetProgress;

    auto rsp = cmd.Execute(req);
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.PayloadLen, 2);
    EXPECT_EQ(rsp.Payload[0], 1);
    EXPECT_EQ(rsp.Payload[1], 42);

    GetOtaProgressCommand unwired(nullptr);
    EXPECT_EQ(unwired.Execute(req).Status, kStatusError);
}

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
    EXPECT_EQ(static_cast<uint8_t>(Cluster::Ota),      0x05);
}
