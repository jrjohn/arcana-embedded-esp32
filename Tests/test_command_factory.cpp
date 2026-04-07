#include <gtest/gtest.h>
#include "CommandFactory.hpp"
#include "ICommand.hpp"

using namespace Arcana::Command;

// Helper: factory with no Sensor / KeyExchangeManager
static CommandFactory makeFactory() {
    CommandFactory::Dependencies deps;
    deps.Sensor = nullptr;
    deps.KeyExchangeMgr = nullptr;
    return CommandFactory(deps);
}

// ── System cluster ──────────────────────────────────────────────────────────

TEST(CommandFactoryTest, CreatePingCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::System, SystemCmd::Ping);
    ASSERT_NE(cmd.get(), nullptr);
}

TEST(CommandFactoryTest, CreateGetDeviceInfoCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::System, SystemCmd::GetDeviceInfo);
    ASSERT_NE(cmd.get(), nullptr);
}

TEST(CommandFactoryTest, SystemUnknownCommand) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.Create(Cluster::System, 0xFF).get(), nullptr);
    EXPECT_EQ(factory.Create(Cluster::System, 0x00).get(), nullptr);
}

// ── Sensor cluster (commands take nullptr Sensor → return error path) ──────

TEST(CommandFactoryTest, CreateGetSensorDataCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Sensor, SensorCmd::GetData);
    ASSERT_NE(cmd.get(), nullptr);
    // Execute with nullptr sensor → error status
    CommandRequest req;
    req.ClusterId = Cluster::Sensor;
    req.Command = SensorCmd::GetData;
    auto rsp = cmd->Execute(req);
    EXPECT_EQ(rsp.Status, kStatusError);
}

TEST(CommandFactoryTest, CreateSetNotifyIntervalCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Sensor, SensorCmd::SetNotifyInterval);
    ASSERT_NE(cmd.get(), nullptr);
    // Execute with nullptr sensor → invalid param
    CommandRequest req;
    req.ClusterId = Cluster::Sensor;
    req.Command = SensorCmd::SetNotifyInterval;
    req.PayloadLen = 4;
    auto rsp = cmd->Execute(req);
    EXPECT_EQ(rsp.Status, kStatusInvalidParam);
}

TEST(CommandFactoryTest, SensorUnknownCommand) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.Create(Cluster::Sensor, 0xFF).get(), nullptr);
}

// ── BLE cluster ─────────────────────────────────────────────────────────────

TEST(CommandFactoryTest, CreateGetBleStatusCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::GetStatus);
    ASSERT_NE(cmd.get(), nullptr);
}

TEST(CommandFactoryTest, CreateSetDeviceNameCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::SetDeviceName);
    ASSERT_NE(cmd.get(), nullptr);
    // Empty payload → invalid param
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::SetDeviceName;
    req.PayloadLen = 0;
    auto rsp = cmd->Execute(req);
    EXPECT_EQ(rsp.Status, kStatusInvalidParam);
}

TEST(CommandFactoryTest, SetDeviceNameTooLongFails) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::SetDeviceName);
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::SetDeviceName;
    req.PayloadLen = 30;  // > 29
    auto rsp = cmd->Execute(req);
    EXPECT_EQ(rsp.Status, kStatusInvalidParam);
}

TEST(CommandFactoryTest, SetDeviceNameValidPayload) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::SetDeviceName);
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::SetDeviceName;
    const char* name = "TestDevice";
    req.PayloadLen = strlen(name);
    memcpy(req.Payload, name, req.PayloadLen);
    auto rsp = cmd->Execute(req);
    // Stub returns ESP_OK → command returns OK
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.PayloadLen, strlen(name));
}

TEST(CommandFactoryTest, CreateBleScanCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::Scan);
    ASSERT_NE(cmd.get(), nullptr);
    EXPECT_TRUE(cmd->IsAsync());  // BleScan is async
}

TEST(CommandFactoryTest, BleScanWithDefaultDuration) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::Scan);
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::Scan;
    req.PayloadLen = 0;
    auto rsp = cmd->Execute(req);
    EXPECT_EQ(rsp.Status, kStatusOk);
    EXPECT_EQ(rsp.PayloadLen, sizeof(uint32_t));
    uint32_t duration = 0;
    memcpy(&duration, rsp.Payload, sizeof(uint32_t));
    EXPECT_EQ(duration, 10u);  // default
}

TEST(CommandFactoryTest, BleScanWithExplicitDuration) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::Scan);
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::Scan;
    uint32_t want = 30;
    memcpy(req.Payload, &want, sizeof(uint32_t));
    req.PayloadLen = sizeof(uint32_t);
    auto rsp = cmd->Execute(req);
    uint32_t got = 0;
    memcpy(&got, rsp.Payload, sizeof(uint32_t));
    EXPECT_EQ(got, 30u);
}

TEST(CommandFactoryTest, BleScanRejectsZeroDuration) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::Scan);
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::Scan;
    uint32_t zero = 0;
    memcpy(req.Payload, &zero, sizeof(uint32_t));
    req.PayloadLen = sizeof(uint32_t);
    auto rsp = cmd->Execute(req);
    // Falls back to default 10
    uint32_t got = 0;
    memcpy(&got, rsp.Payload, sizeof(uint32_t));
    EXPECT_EQ(got, 10u);
}

TEST(CommandFactoryTest, BleScanRejectsExcessiveDuration) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Ble, BleCmd::Scan);
    CommandRequest req;
    req.ClusterId = Cluster::Ble;
    req.Command = BleCmd::Scan;
    uint32_t huge = 1000;  // > 300
    memcpy(req.Payload, &huge, sizeof(uint32_t));
    req.PayloadLen = sizeof(uint32_t);
    auto rsp = cmd->Execute(req);
    uint32_t got = 0;
    memcpy(&got, rsp.Payload, sizeof(uint32_t));
    EXPECT_EQ(got, 10u);  // clamped
}

TEST(CommandFactoryTest, BleUnknownCommand) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.Create(Cluster::Ble, 0xFF).get(), nullptr);
}

// ── MQTT cluster ────────────────────────────────────────────────────────────

TEST(CommandFactoryTest, CreateGetMqttStatusCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Mqtt, MqttCmd::GetStatus);
    ASSERT_NE(cmd.get(), nullptr);
    // Factory caches the pointer
    EXPECT_NE(factory.MqttStatusCmd(), nullptr);
}

TEST(CommandFactoryTest, MqttUnknownCommand) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.Create(Cluster::Mqtt, 0xFF).get(), nullptr);
}

// ── Security cluster ────────────────────────────────────────────────────────

TEST(CommandFactoryTest, CreateKeyExchangeCommand) {
    auto factory = makeFactory();
    auto cmd = factory.Create(Cluster::Security, SecurityCmd::KeyExchange);
    ASSERT_NE(cmd.get(), nullptr);
    // Execute with nullptr KeyExchangeManager → error
    CommandRequest req;
    req.ClusterId = Cluster::Security;
    req.Command = SecurityCmd::KeyExchange;
    req.PayloadLen = 64;
    auto rsp = cmd->Execute(req);
    EXPECT_EQ(rsp.Status, kStatusError);
}

TEST(CommandFactoryTest, SecurityUnknownCommand) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.Create(Cluster::Security, 0xFF).get(), nullptr);
}

// ── Invalid cluster ─────────────────────────────────────────────────────────

TEST(CommandFactoryTest, InvalidClusterReturnsNullptr) {
    auto factory = makeFactory();
    auto cmd = factory.Create(static_cast<Cluster>(0xFF), 0x01);
    EXPECT_EQ(cmd.get(), nullptr);
}
