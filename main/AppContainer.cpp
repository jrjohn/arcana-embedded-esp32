#include "AppContainer.hpp"
#include "esp_wifi.h"
#include "impl/SensorServiceImpl.hpp"
#include "impl/BleTransportServiceImpl.hpp"
#include "impl/MqttTransportServiceImpl.hpp"
#include "impl/LedServiceImpl.hpp"
#include "impl/LcdServiceImpl.hpp"
#include "impl/TimerServiceImpl.hpp"
#include "impl/DiagnosticServiceImpl.hpp"
#include "impl/CommandBridgeServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "impl/OtaServiceImpl.hpp"
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/WifiServiceImpl.hpp"
#include "esp_log.h"

static const char* TAG = "AppContainer";

// Upload monitor task function (static, outside class)
static void uploadMonTask(void* param) {
    struct Ctx { Arcana::Io::IoService* io; Arcana::Upload::HttpUploadService* upload; Arcana::Mqtt::MqttTransportService* mqtt; };
    auto* ctx = static_cast<Ctx*>(param);
    ESP_LOGI("UploadMon", "Task started");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (ctx->io && ctx->io->isUploadRequested()) {
            ctx->io->clearUploadRequest();
            ESP_LOGI("UploadMon", "Upload — disconnecting MQTT...");
            if (ctx->mqtt) ctx->mqtt->stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            if (ctx->upload) {
                uint8_t n = ctx->upload->uploadPendingFiles();
                ESP_LOGI("UploadMon", "Upload complete: %u files", n);
            }
            ESP_LOGI("UploadMon", "Reconnecting MQTT...");
            if (ctx->mqtt) ctx->mqtt->start();
        }
    }
}

namespace Arcana {

// Static MVVM instances (same lifetime as AppContainer)
static Lcd::LcdViewModel sViewModel;
static Lcd::MainView sMainView;

AppContainer& AppContainer::getInstance() {
    static AppContainer sInstance;
    return sInstance;
}

void AppContainer::run() {
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    wireServices();
    initHAL();
    wireViews();   // after initHAL — display hardware must exist before wiring
    initServices();

    // Wi-Fi connect + NTP sync (via WifiService)
    ESP_ERROR_CHECK(mWifi->connect());
    mWifi->syncNtp(10000);

    startServices();

    // Wait for AtsStorage to be ready (device.ats needed for credential persistence)
    if (mStorage) {
        auto& storageImpl = static_cast<Storage::AtsStorageServiceImpl&>(*mStorage);
        for (int i = 0; i < 20 && !storageImpl.isReady(); i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    // Device registration (TOFU) — after AtsStorage ready
    if (mStorage && mReg->doRegistration()) {
        ESP_LOGI(TAG, "Device registered: %s -> %s:%u",
                 mReg->deviceId(), mReg->credentials().mqttBroker,
                 mReg->credentials().mqttPort);
    } else {
        ESP_LOGW(TAG, "Registration failed — using hardcoded MQTT config");
    }

    ESP_LOGI(TAG, "All services running");

    // Upload monitor — reuse main task with toast + cancel support
    {
        auto* io = mIo;
        auto* upload = mUpload;
        auto* mqtt = mMqtt;
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (io && io->isUploadRequested()) {
                io->clearUploadRequest();

                // Show toast + arm cancel (Button A again = cancel)
                sViewModel.showToast("Uploading...", 0);
                io->armCancel();

                // Disable WiFi power save for reliable TCP throughput
                esp_wifi_set_ps(WIFI_PS_NONE);

                // Pause ECG recording FIRST to relieve heap pressure
                // (ECG 1KHz consumes ~28KB/s; MQTT stop needs heap for free)
                if (mStorage) {
                    static_cast<Storage::AtsStorageServiceImpl&>(*mStorage).pauseRecording();
                    vTaskDelay(pdMS_TO_TICKS(300));  // let ECG task yield + flush
                }
                ESP_LOGI(TAG, "Button A: upload — heap=%lu, disconnecting MQTT...",
                         (unsigned long)esp_get_free_heap_size());
                if (mqtt) mqtt->stop();
                vTaskDelay(pdMS_TO_TICKS(500));

                if (upload) {
                    // Set progress callback → updates LCD toast
                    upload->setProgressCallback(
                        [](uint8_t curFile, uint8_t totalFiles,
                           uint32_t bytesSent, uint32_t totalBytes, void*) {
                            char msg[22];
                            uint8_t pct = totalBytes > 0
                                ? (uint8_t)(bytesSent * 100ULL / totalBytes) : 0;
                            snprintf(msg, sizeof(msg), "Upload %u/%u  %u%%",
                                     curFile, totalFiles, pct);
                            sViewModel.showToast(msg, 0);
                        }, nullptr);

                    uint8_t n = upload->uploadPendingFiles();
                    upload->setProgressCallback(nullptr, nullptr);
                    ESP_LOGI(TAG, "Upload complete: %u files", n);
                }

                io->disarmCancel();
                sViewModel.showToast("Upload done!", 3000);

                ESP_LOGI(TAG, "Reconnecting MQTT...");
                if (mqtt) mqtt->start();

                // Resume ECG recording (safe even if uploadPendingFiles already resumed)
                if (mStorage) {
                    static_cast<Storage::AtsStorageServiceImpl&>(*mStorage).resumeRecording();
                }

                // Re-enable WiFi power save
                esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
            }
        }
    }
}

void AppContainer::wireServices() {
    // Get service singletons
    mTimer   = &Timer::TimerServiceImpl::getInstance();
    mSensor  = &Sensor::SensorServiceImpl::getInstance();
    mBle     = &Ble::BleTransportServiceImpl::getInstance();
    mMqtt    = &Mqtt::MqttTransportServiceImpl::getInstance();
    mLed     = &Led::LedServiceImpl::getInstance();
    mLcd     = &Lcd::LcdServiceImpl::getInstance();
    mBridge  = &CommandBridgeServiceImpl::getInstance();
    mCommand = &Command::CommandService::Instance();
    mDiag    = &Diagnostic::DiagnosticServiceImpl::getInstance();
    mStorage = &Storage::AtsStorageServiceImpl::getInstance();
    mIo      = &Io::IoServiceImpl::getInstance();
    mOta     = &OtaServiceImpl::getInstance();
    mReg     = &Registration::RegistrationServiceImpl::getInstance();
    mUpload  = &Upload::HttpUploadServiceImpl::getInstance();
    mWifi    = &Wifi::WifiServiceImpl::getInstance();

    // Wire Storage <- Sensor (record sensor data to SD)
    mStorage->input.SensorDataEvents = mSensor->output.DataEvents;

    // Wire LED <- Timer (base tick for 1-second cycling)
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire Diagnostic <- Timer (base tick for 10-second interval)
    mDiag->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire BLE <- Sensor
    mBle->input.SensorDataEvents = mSensor->output.DataEvents;

    // Wire MQTT <- Sensor (publish temperature/humidity)
    mMqtt->input.SensorDataEvents = mSensor->output.DataEvents;

    // Wire Command <- Sensor
    mCommand->input.Sensor = mSensor->output.Sensor;

    ESP_LOGI(TAG, "Services wired");
}

void AppContainer::wireViews() {
    // ViewModel subscribes to Service outputs (not the View!)
    sViewModel.input.SensorData = mSensor->output.DataEvents;
    sViewModel.input.StorageStats = mStorage ? mStorage->output.StatsEvents : nullptr;
    sViewModel.input.BaseTimer = mTimer->output.BaseTimer;

    // View receives ViewModel + display hardware
    sMainView.input.viewModel = &sViewModel;
    sMainView.input.display = &mLcd->getDisplay();

    ESP_LOGI(TAG, "Views wired");
}

void AppContainer::initHAL() {
    // Phase 1: Hardware initialization (order matters)
    ESP_ERROR_CHECK(mTimer->init_HAL());
    ESP_ERROR_CHECK(mSensor->init_HAL());
    ESP_ERROR_CHECK(mBle->init_HAL());
    ESP_ERROR_CHECK(mLed->init_HAL());
    ESP_ERROR_CHECK(mLcd->init_HAL());
    ESP_ERROR_CHECK(mMqtt->init_HAL());
    if (mStorage->init_HAL() != ESP_OK) {
        ESP_LOGW(TAG, "SD card unavailable — storage disabled");
        mStorage = nullptr;
    }
    ESP_ERROR_CHECK(mIo->init_HAL());

    ESP_LOGI(TAG, "HAL initialized");
}

void AppContainer::initServices() {
    // Phase 2: Logic + subscriptions
    ESP_ERROR_CHECK(mTimer->init());

    // Sensor first (others may depend on its output)
    ESP_ERROR_CHECK(mSensor->init());

    // BLE init (subscribes to sensor data)
    ESP_ERROR_CHECK(mBle->init());

    // Command service init (creates factory, dispatcher, key exchange)
    ESP_ERROR_CHECK(mCommand->init());

    // Wire bridge inputs (must be after command init, BLE init, MQTT init)
    mBridge->input.BleConnectionEvents = mBle->output.ConnectionEvents;
    mBridge->input.BleCommandWriteEvents = mBle->output.CommandWriteEvents;
    mBridge->input.MqttCommandEvents = mMqtt->output.CommandEvents;
    mBridge->input.MqttConnectionStatus = mMqtt->output.ConnectionStatus;
    mBridge->input.CommandResponseEvents = mCommand->output.ResponseEvents;
    mBridge->input.KeyExchangeMgr = mCommand->output.KeyExchangeMgr;
    mBridge->input.Factory = mCommand->output.Factory;
    mBridge->input.MqttTransport = mMqtt;
    mBridge->input.BleServer = &static_cast<Ble::BleTransportServiceImpl&>(*mBle).server();

    // Bridge init (sets up all subscriptions)
    ESP_ERROR_CHECK(mBridge->init());

    // LED init (no dependencies)
    ESP_ERROR_CHECK(mLed->init());

    // LCD init (hardware only, no subscriptions)
    ESP_ERROR_CHECK(mLcd->init());

    // MQTT init (subscribes to sensor data)
    ESP_ERROR_CHECK(mMqtt->init());

    // Diagnostic init (subscribes to timer for periodic logging)
    ESP_ERROR_CHECK(mDiag->init());

    // Storage init (semaphore + mutex)
    if (mStorage) ESP_ERROR_CHECK(mStorage->init());

    // IO init
    ESP_ERROR_CHECK(mIo->init());

    ESP_LOGI(TAG, "Services initialized");
}

void AppContainer::startServices() {
    ESP_ERROR_CHECK(mTimer->start());
    ESP_ERROR_CHECK(mSensor->start());
    ESP_ERROR_CHECK(mBle->start());
    ESP_ERROR_CHECK(mCommand->Start());
    ESP_ERROR_CHECK(mLed->start());
    ESP_ERROR_CHECK(mLcd->start());
    ESP_ERROR_CHECK(mMqtt->start());
    ESP_ERROR_CHECK(mDiag->start());
    if (mStorage) ESP_ERROR_CHECK(mStorage->start());
    ESP_ERROR_CHECK(mIo->start());

    // Start MVVM: View render task first, then ViewModel subscribes + notifies
    sMainView.start();
    sViewModel.init(sMainView.taskHandle());

    ESP_LOGI(TAG, "Services started");
}

} // namespace Arcana
