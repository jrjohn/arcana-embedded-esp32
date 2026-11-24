#pragma once

#include "DriverService.hpp"
#include "SensorService.hpp"  // demo
#include "BleTransportService.hpp"  // demo
#include "MqttTransportService.hpp"
#include "LedService.hpp"  // demo
#include "CommandBridgeService.hpp"  // demo
#include "CommandService.hpp"  // demo
#include "TimerService.hpp"
#include "LcdService.hpp"  // demo
#include "MainViewModel.hpp"  // demo
#include "MainView.hpp"  // demo
#include "DiagnosticService.hpp"
#include "AtsStorageService.hpp"  // demo
#include "IoService.hpp"  // demo
#include "OtaService.hpp"
#include "RegistrationService.hpp"  // demo
#include "HttpUploadService.hpp"  // demo
#include "WifiService.hpp"
#include "FreezeGuardService.hpp"

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
    void wireViews();  // demo
    void initHAL();
    void initServices();
    void startServices();

    Driver::DriverService* mDriver = nullptr;
    Timer::TimerService* mTimer = nullptr;
    Sensor::SensorService* mSensor = nullptr;  // demo
    Ble::BleTransportService* mBle = nullptr;  // demo
    Mqtt::MqttTransportService* mMqtt = nullptr;
    Led::LedService* mLed = nullptr;  // demo
    CommandBridgeService* mBridge = nullptr;  // demo
    Lcd::LcdService* mLcd = nullptr;  // demo
    Command::CommandService* mCommand = nullptr;  // demo
    Diagnostic::DiagnosticService* mDiag = nullptr;
    Storage::AtsStorageService* mStorage = nullptr;  // demo
    Io::IoService* mIo = nullptr;  // demo
    OtaService* mOta = nullptr;
    Registration::RegistrationService* mReg = nullptr;  // demo
    Upload::HttpUploadService* mUpload = nullptr;  // demo
    Wifi::WifiService* mWifi = nullptr;
    Freeze::FreezeGuardService* mFreeze = nullptr;
};

} // namespace Arcana
