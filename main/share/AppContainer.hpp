#pragma once

#include "DriverService.hpp"
#include "SensorService.hpp"
#include "BleTransportService.hpp"
#include "MqttTransportService.hpp"
#include "LedService.hpp"
#include "CommandBridgeService.hpp"
#include "CommandService.hpp"
#include "TimerService.hpp"
#include "LcdService.hpp"
#include "MainViewModel.hpp"
#include "MainView.hpp"
#include "DiagnosticService.hpp"
#include "AtsStorageService.hpp"
#include "IoService.hpp"
#include "OtaService.hpp"
#include "RegistrationService.hpp"
#include "HttpUploadService.hpp"
#include "WifiService.hpp"

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
    void wireViews();
    void initHAL();
    void initServices();
    void startServices();

    Driver::DriverService* mDriver = nullptr;
    Timer::TimerService* mTimer = nullptr;
    Sensor::SensorService* mSensor = nullptr;
    Ble::BleTransportService* mBle = nullptr;
    Mqtt::MqttTransportService* mMqtt = nullptr;
    Led::LedService* mLed = nullptr;
    CommandBridgeService* mBridge = nullptr;
    Lcd::LcdService* mLcd = nullptr;
    Command::CommandService* mCommand = nullptr;
    Diagnostic::DiagnosticService* mDiag = nullptr;
    Storage::AtsStorageService* mStorage = nullptr;
    Io::IoService* mIo = nullptr;
    OtaService* mOta = nullptr;
    Registration::RegistrationService* mReg = nullptr;
    Upload::HttpUploadService* mUpload = nullptr;
    Wifi::WifiService* mWifi = nullptr;
};

} // namespace Arcana
