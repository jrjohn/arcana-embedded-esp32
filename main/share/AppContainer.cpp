#include "AppContainer.hpp"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "impl/SensorServiceImpl.hpp"
#include "impl/BleTransportServiceImpl.hpp"
#include "impl/MqttTransportServiceImpl.hpp"
#include "impl/LedServiceImpl.hpp"
#include "impl/LcdServiceImpl.hpp"
#include "impl/TimerServiceImpl.hpp"
#include "impl/DiagnosticServiceImpl.hpp"
#include "impl/CommandBridgeServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/SdBenchmarkServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "impl/OtaServiceImpl.hpp"
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/WifiServiceImpl.hpp"
#include "impl/DriverServiceImpl.hpp"
#include "view/BootSplash.hpp"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char* TAG = "AppContainer";

namespace Arcana {

// Static MVVM instances (same lifetime as AppContainer)
static Lcd::MainViewModel sViewModel;
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

#if CONFIG_ATS_SD_BENCHMARK_ON_BOOT
    // Diagnostics: one-shot sequential-write benchmark. Runs here — card
    // mounted (initHAL) but the ATS writer task not yet started — so the
    // benchmark has exclusive card access; concurrent ECG writes both skew
    // the number and can fail the benchmark's fwrite mid-run.
    if (mStorage) {
        ESP_LOGI(TAG, "SD benchmark: %d ms sequential write...",
                 CONFIG_ATS_SD_BENCHMARK_DURATION_MS);
        auto bench = SdBench::SdBenchmarkServiceImpl::getInstance()
                         .runBenchmark(CONFIG_ATS_SD_BENCHMARK_DURATION_MS);
        if (!bench.error) {
            ESP_LOGI(TAG, "SD benchmark: %lu.%lu KB/s, %lu KB total, %lu blocks/s",
                     (unsigned long)(bench.speedKBps10 / 10),
                     (unsigned long)(bench.speedKBps10 % 10),
                     (unsigned long)bench.totalKB,
                     (unsigned long)bench.recordsPerSec);
        } else {
            ESP_LOGE(TAG, "SD benchmark failed");
        }
    } else {
        ESP_LOGW(TAG, "SD benchmark skipped — storage unavailable");
    }
#endif

    // Wi-Fi connect with retry (don't crash on temporary AP failure)
    for (int attempt = 1; attempt <= 5; attempt++) {
        esp_err_t wifiErr = mWifi->connect();
        if (wifiErr == ESP_OK) break;
        ESP_LOGW(TAG, "WiFi connect failed (%s), retry %d/5...",
                 esp_err_to_name(wifiErr), attempt);
        if (attempt == 5) {
            ESP_LOGE(TAG, "WiFi connect failed after 5 attempts — continuing without network");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    if (mWifi->isConnected()) {
        mWifi->syncNtp(10000);
    }

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
        // Re-key the command channel with the per-device ECDH key from
        // registration, replacing the compile-time bootstrap PSK. The server
        // (and authorized operators) derive the identical key, so the static
        // fleet-wide secret is no longer the operational key. No-op when command
        // encryption is disabled or no commKey was provisioned.
        if (mBridge && mReg->credentials().valid && mReg->credentials().hasCommKey) {
            mBridge->SetCommandKey(mReg->credentials().commKey);
            ESP_LOGI(TAG, "Command channel re-keyed with per-device key");
        }
    } else {
        ESP_LOGW(TAG, "Registration failed — using hardcoded MQTT config");
    }

    ESP_LOGI(TAG, "All services running");

    // OTA rollback gate: if this image is on probation (first boot after an
    // update), everything above succeeding is the self-test — accept it.
    // Crashing before this line leaves the image PENDING_VERIFY and the
    // bootloader reverts to the previous slot on the next reset.
    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state;
        if (running &&
            esp_ota_get_state_partition(running, &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "OTA image verified — rollback cancelled");
        }
    }


    // Upload monitor + WiFi watchdog — reuse main task
    {
        auto* io = mIo;
        auto* upload = mUpload;
        auto* mqtt = mMqtt;
        uint32_t lastWifiCheck = xTaskGetTickCount();
        static const uint32_t WIFI_CHECK_INTERVAL = pdMS_TO_TICKS(5 * 60 * 1000); // 5 min

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(500));

            // WiFi watchdog: check every 5 minutes, reconnect if needed
            if ((xTaskGetTickCount() - lastWifiCheck) >= WIFI_CHECK_INTERVAL) {
                lastWifiCheck = xTaskGetTickCount();
                if (mWifi && !mWifi->isConnected()) {
                    ESP_LOGW(TAG, "WiFi disconnected — reconnecting...");
                    for (int i = 1; i <= 5; i++) {
                        if (mWifi->connect() == ESP_OK) {
                            ESP_LOGI(TAG, "WiFi reconnected");
                            mWifi->syncNtp(10000);
                            break;
                        }
                        ESP_LOGW(TAG, "WiFi reconnect retry %d/5...", i);
                        vTaskDelay(pdMS_TO_TICKS(3000));
                    }
                }
            }

            if (io && io->isUploadRequested()) {
                io->clearUploadRequest();

                // 1. Pause ECG immediately (stop SD writes + free CPU)
                if (mStorage) {
                    static_cast<Storage::AtsStorageServiceImpl&>(*mStorage).pauseRecording();
                }
                sViewModel.showToast("Uploading...", 0);
                io->armCancel();
                vTaskDelay(pdMS_TO_TICKS(300));  // let ECG task yield

                // 2. WiFi power save off
                esp_wifi_set_ps(WIFI_PS_NONE);

                // 3. Stop MQTT (needs heap, but ECG is paused so safe)
                ESP_LOGI(TAG, "Upload: stopping MQTT (heap=%lu)...",
                         (unsigned long)esp_get_free_heap_size());
                if (mqtt) mqtt->stop();
                vTaskDelay(pdMS_TO_TICKS(300));

                // 4. Deinit BLE stack to free ~50KB for TLS
                if (mBle) mBle->stop();
                esp_bluedroid_disable();
                esp_bluedroid_deinit();
                esp_bt_controller_disable();
                esp_bt_controller_deinit();
                ESP_LOGI(TAG, "Upload: BLE off, heap=%lu",
                         (unsigned long)esp_get_free_heap_size());

                if (upload) {
                    ESP_LOGI(TAG, "Starting upload (heap=%lu)...",
                             (unsigned long)esp_get_free_heap_size());

                    upload->setProgressCallback(
                        [](uint8_t curFile, uint8_t totalFiles,
                           uint32_t bytesSent, uint32_t totalBytes, void*) {
                            char msg[22];
                            if (totalBytes > 0) {
                                uint32_t pctX10 = (uint32_t)(bytesSent * 1000ULL / totalBytes);
                                snprintf(msg, sizeof(msg), "%u/%u  %u.%u%%",
                                         curFile, totalFiles,
                                         (unsigned)(pctX10 / 10),
                                         (unsigned)(pctX10 % 10));
                            } else {
                                snprintf(msg, sizeof(msg), "%u/%u", curFile, totalFiles);
                            }
                            sViewModel.showToast(msg, 0);
                        }, nullptr);

                    uint8_t n = upload->uploadPendingFiles();
                    upload->setProgressCallback(nullptr, nullptr);
                    ESP_LOGI(TAG, "Upload complete: %u files", n);
                }

                bool wasCancelled = io->isCancelRequested();
                io->disarmCancel();

                // BLE was deinited — must restart to recover cleanly
                // (reinit in same boot leaks ~2KB, known ESP-IDF issue)
                if (wasCancelled) {
                    sViewModel.showToast("Cancelled.Reboot", 2000);
                    ESP_LOGI(TAG, "Upload cancelled — restarting");
                } else {
                    sViewModel.showToast("Done! Reboot..", 2000);
                    ESP_LOGI(TAG, "Upload done — restarting");
                }
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }
    }
}

void AppContainer::wireServices() {
    // Get service singletons
    mDriver  = &Driver::DriverServiceImpl::getInstance();
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

    // Wire Command <- OTA (Ota::StartUpdate / GetProgress)
    mCommand->input.Ota = mOta;

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
    // Phase 1: Hardware initialization (order matters).
    // Board-level resources first: the XL9555 expander must be up (and its
    // latched INT released) before the LCD and SD card touch SPI2.
    ESP_ERROR_CHECK(mDriver->init_HAL());

    ESP_ERROR_CHECK(mTimer->init_HAL());
    ESP_ERROR_CHECK(mSensor->init_HAL());
    ESP_ERROR_CHECK(mBle->init_HAL());
    ESP_ERROR_CHECK(mLed->init_HAL());
    if (mLcd->init_HAL() != ESP_OK) {
        // Boards without an SSD1306 (e.g. DNESP32S3) — views keep rendering
        // into the framebuffer; Ssd1306 skips I2C traffic when not ready.
        ESP_LOGW(TAG, "OLED unavailable — display output disabled");
    }
    // LCD is up. The SD-card init below can stall for tens of seconds on a cold
    // boot (busy-card retry) — animate a splash so the panel isn't black/hung
    // looking. Handed off to MainView in startServices().
    View::BootSplash::start(&mLcd->getDisplay());
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

    // Stop the boot splash and wait for it to release the panel before MainView
    // takes over (they share the one framebuffer).
    View::BootSplash::stop();

    // Start MVVM: View render task first, then ViewModel subscribes + notifies
    sMainView.start();
    sViewModel.init(sMainView.taskHandle());

    ESP_LOGI(TAG, "Services started");
}

} // namespace Arcana
