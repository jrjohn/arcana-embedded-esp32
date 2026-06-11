#pragma once

#include "Observable.hpp"
#include "BleTypes.hpp"
#include "BleGattServer.hpp"
#include "MqttTypes.hpp"
#include "CommandTypes.hpp"
#include "CommandCodec.hpp"
#include "CommandFactory.hpp"
#include "MqttTransportService.hpp"
#include "esp_err.h"

namespace Arcana {

class CommandBridgeService {
public:
    struct Input {
        // BLE events
        Observable<Ble::BleConnectionEvent>* BleConnectionEvents = nullptr;
        Observable<Ble::BleCommandWriteEvent>* BleCommandWriteEvents = nullptr;

        // MQTT events
        Observable<Mqtt::MqttCommandEvent>* MqttCommandEvents = nullptr;
        Observable<Mqtt::MqttConnectionStatus>* MqttConnectionStatus = nullptr;

        // Command response events
        Observable<Command::CommandResponse>* CommandResponseEvents = nullptr;

        // Service references
        Command::KeyExchangeManager* KeyExchangeMgr = nullptr;
        Command::CommandFactory* Factory = nullptr;
        Mqtt::MqttTransportService* MqttTransport = nullptr;
        Ble::BleGattServer* BleServer = nullptr;
    };

    struct Output {};

    Input input;
    Output output;

    virtual ~CommandBridgeService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    // Re-key the command channel with the per-device key from registration.
    // Default no-op (encryption disabled / test doubles); overridden by the impl.
    virtual void SetCommandKey(const uint8_t /*key*/[32]) {}
};

} // namespace Arcana
