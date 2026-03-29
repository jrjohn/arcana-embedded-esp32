#pragma once

#include "SensorService.hpp"
#include "BleTransportService.hpp"
#include "MqttTransportService.hpp"
#include "LedService.hpp"
#include "CommandBridgeService.hpp"
#include "CommandService.hpp"
#include "TimerService.hpp"
#include "LcdService.hpp"
#include "DiagnosticService.hpp"

namespace Arcana {

class AppContainer {
public:
    static AppContainer& getInstance();

    void run();

private:
    AppContainer() = default;
    ~AppContainer() = default;
    AppContainer(const AppContainer&) = delete;
    AppContainer& operator=(const AppContainer&) = delete;

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
    Lcd::LcdService* mLcd = nullptr;
    Command::CommandService* mCommand = nullptr;
    Diagnostic::DiagnosticService* mDiag = nullptr;
};

} // namespace Arcana
