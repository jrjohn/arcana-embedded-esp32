#pragma once

#include "SensorService.hpp"
#include "BleTransportService.hpp"
#include "MqttTransportService.hpp"
#include "LedService.hpp"
#include "CommandBridgeService.hpp"
#include "CommandService.hpp"
#include "TimerService.hpp"

namespace Arcana {

class Controller {
public:
    static Controller& getInstance();

    void run();

private:
    Controller() = default;
    ~Controller() = default;
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    void wireServices();
    void initHAL();
    void initServices();
    void startServices();

    Timer::TimerService* mTimer = nullptr;
    Sensor::SensorService* mSensor = nullptr;
    Ble::BleTransportService* mBle = nullptr;
    Mqtt::MqttTransportService* mMqtt = nullptr;
    Led::LedService* mLed = nullptr;
    CommandBridgeService* mBridge = nullptr;
    Command::CommandService* mCommand = nullptr;
};

} // namespace Arcana
