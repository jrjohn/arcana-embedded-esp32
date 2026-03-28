#include "CommandBridgeServiceImpl.hpp"
#include "commands/GetMqttStatusCommand.hpp"
#include "FrameCodec.hpp"
#include "esp_log.h"

static const char* TAG = "CommandBridge";

// Max encoded response: protobuf + crypto overhead + frame overhead
static constexpr size_t kMaxEncodedResponseLen = FrameCodec::kMaxPayloadLen + FrameCodec::kOverhead;

#ifdef CONFIG_MQTT_SVC_RSP_TOPIC
static constexpr const char* sRspTopic = CONFIG_MQTT_SVC_RSP_TOPIC;
#else
static constexpr const char* sRspTopic = "arcana/rsp";
#endif

namespace Arcana {

CommandBridgeService& CommandBridgeServiceImpl::getInstance() {
    static CommandBridgeServiceImpl sInstance;
    return sInstance;
}

esp_err_t CommandBridgeServiceImpl::init_HAL() {
    ESP_LOGI(TAG, "HAL initialized");
    return ESP_OK;
}

esp_err_t CommandBridgeServiceImpl::init() {
    // Init codec (reads Kconfig, init crypto if enabled)
    ESP_ERROR_CHECK(mCodec.Init());

    // Wire KeyExchangeManager to codec for session-aware encrypt/decrypt
    if (input.KeyExchangeMgr) {
        mCodec.SetKeyExchangeManager(input.KeyExchangeMgr);
    }

    // BLE disconnect -> remove ECDH session
    if (input.BleConnectionEvents) {
        input.BleConnectionEvents->Subscribe(
            [this](const Ble::BleConnectionEvent& evt) {
                if (evt.State == Ble::ConnectionState::Disconnected) {
                    if (input.KeyExchangeMgr) {
                        input.KeyExchangeMgr->RemoveSession(
                            Command::CommandSource::BLE, evt.ConnId);
                    }
                }
            }
        );
    }

    // BLE command writes -> decode -> CommandService
    if (input.BleCommandWriteEvents) {
        input.BleCommandWriteEvents->Subscribe(
            [this](const Ble::BleCommandWriteEvent& evt) {
                Command::CommandRequest req;
                if (mCodec.DecodeRequest(Command::CommandSource::BLE, evt.first,
                                          evt.second.data(), evt.second.size(), req)) {
                    ESP_LOGI(TAG, "BLE command: cluster=0x%02x cmd=0x%02x conn_id=%d",
                             static_cast<uint8_t>(req.ClusterId), req.Command, req.ConnectionId);
                    Command::CommandService::Instance().HandleRequest(req);
                } else {
                    ESP_LOGW(TAG, "Failed to parse BLE command");
                }
            }
        );
    }

    // MQTT command events -> decode -> CommandService
    if (input.MqttCommandEvents) {
        input.MqttCommandEvents->Subscribe(
            [this](const Mqtt::MqttCommandEvent& evt) {
                Command::CommandRequest req;
                if (mCodec.DecodeRequest(Command::CommandSource::MQTT, 0,
                                          evt.Data, evt.Len, req)) {
                    ESP_LOGI(TAG, "MQTT command: cluster=0x%02x cmd=0x%02x",
                             static_cast<uint8_t>(req.ClusterId), req.Command);
                    Command::CommandService::Instance().HandleRequest(req);
                } else {
                    ESP_LOGW(TAG, "Failed to parse MQTT command");
                }
            }
        );
    }

    // MQTT connection status -> update GetMqttStatusCommand
    if (input.MqttConnectionStatus) {
        input.MqttConnectionStatus->Subscribe(
            [this](const Mqtt::MqttConnectionStatus& status) {
                if (input.Factory && input.Factory->MqttStatusCmd()) {
                    input.Factory->MqttStatusCmd()->SetConnected(status.Connected);
                }
            }
        );
    }

    // Command responses -> encode -> BLE / MQTT
    if (input.CommandResponseEvents) {
        input.CommandResponseEvents->Subscribe(
            [this](const Command::CommandResponse& rsp) {
                uint8_t buf[kMaxEncodedResponseLen];
                size_t outLen = 0;
                if (!mCodec.EncodeResponse(rsp, buf, sizeof(buf), outLen)) return;

                switch (rsp.Source) {
                case Command::CommandSource::BLE:
                    if (input.BleServer) {
                        input.BleServer->SendCommandResponse(
                            rsp.ConnectionId, buf, static_cast<uint16_t>(outLen));
                        ESP_LOGI(TAG, "BLE response: cluster=0x%02x cmd=0x%02x status=%d len=%zu",
                                 static_cast<uint8_t>(rsp.ClusterId), rsp.Command, rsp.Status, outLen);
                    }
                    break;
                case Command::CommandSource::MQTT:
                    if (input.MqttTransport) {
                        input.MqttTransport->publish(sRspTopic, buf, outLen, 1);
                        ESP_LOGI(TAG, "MQTT response: cluster=0x%02x cmd=0x%02x len=%zu",
                                 static_cast<uint8_t>(rsp.ClusterId), rsp.Command, outLen);
                    }
                    break;
                default:
                    ESP_LOGD(TAG, "Internal response: cluster=0x%02x cmd=0x%02x",
                             static_cast<uint8_t>(rsp.ClusterId), rsp.Command);
                    break;
                }
            }
        );
    }

    ESP_LOGI(TAG, "Initialized - BLE + MQTT bridges wired");
    return ESP_OK;
}

esp_err_t CommandBridgeServiceImpl::start() {
    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void CommandBridgeServiceImpl::stop() {
    ESP_LOGI(TAG, "Stopped");
}

} // namespace Arcana
