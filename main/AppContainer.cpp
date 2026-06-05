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
#include "esp_log.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "driver/i2c_master.h"
#endif

static const char* TAG = "AppContainer";

#if CONFIG_IDF_TARGET_ESP32S3
// DNESP32S3: the XL9555 IO expander's open-drain INT line latches low after
// power-up until its input registers are read. With a P5 jumper bridging
// IIC_INT onto SPI_MISO (IO13), a latched INT holds MISO down and every SD
// transaction fails CRC. Read (and discard) both input ports once at boot to
// release INT. Uses a scoped I2C bus — created and deleted before any other
// I2C0 user initializes.
static void releaseXl9555Int() {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.scl_io_num = GPIO_NUM_42;
    bus_cfg.sda_io_num = GPIO_NUM_41;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) return;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = 0x20;  // XL9555, A0-A2 strapped to GND
    dev_cfg.scl_speed_hz = 100000;

    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK) {
        uint8_t reg = 0x00;          // input port 0 (auto-increments to port 1)
        uint8_t in[2] = {};
        esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, in, 2, 100);
        ESP_LOGI(TAG, "XL9555 INT release: %s (in0=0x%02x in1=0x%02x)",
                 esp_err_to_name(err), in[0], in[1]);
        i2c_master_bus_rm_device(dev);
    }
    i2c_del_master_bus(bus);
}
#endif

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
    } else {
        ESP_LOGW(TAG, "Registration failed — using hardcoded MQTT config");
    }

    ESP_LOGI(TAG, "All services running");

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
#if CONFIG_IDF_TARGET_ESP32S3
    releaseXl9555Int();  // must precede SD init — see comment at definition
#endif

    // Phase 1: Hardware initialization (order matters)
    ESP_ERROR_CHECK(mTimer->init_HAL());
    ESP_ERROR_CHECK(mSensor->init_HAL());
    ESP_ERROR_CHECK(mBle->init_HAL());
    ESP_ERROR_CHECK(mLed->init_HAL());
    if (mLcd->init_HAL() != ESP_OK) {
        // Boards without an SSD1306 (e.g. DNESP32S3) — views keep rendering
        // into the framebuffer; Ssd1306 skips I2C traffic when not ready.
        ESP_LOGW(TAG, "OLED unavailable — display output disabled");
    }
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
