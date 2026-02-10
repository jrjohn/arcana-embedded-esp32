#include "Controller.hpp"
#include "SensorServiceImpl.hpp"
#include "BleTransportServiceImpl.hpp"
#include "MqttTransportServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "TimerServiceImpl.hpp"
#include "CommandBridgeServiceImpl.hpp"
#include "protocol_examples_common.h"
#include "esp_log.h"

static const char* TAG = "Controller";

namespace Arcana {

Controller& Controller::getInstance() {
    static Controller sInstance;
    return sInstance;
}

void Controller::run() {
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
    initServices();

    // Wi-Fi must be up before MQTT
    ESP_ERROR_CHECK(example_connect());

    startServices();

    ESP_LOGI(TAG, "All services running");
}

void Controller::wireServices() {
    // Get service singletons
    mTimer   = &Timer::TimerServiceImpl::getInstance();
    mSensor  = &Sensor::SensorServiceImpl::getInstance();
    mBle     = &Ble::BleTransportServiceImpl::getInstance();
    mMqtt    = &Mqtt::MqttTransportServiceImpl::getInstance();
    mLed     = &Led::LedServiceImpl::getInstance();
    mBridge  = &CommandBridgeServiceImpl::getInstance();
    mCommand = &Command::CommandService::Instance();

    // Wire LED <- Timer
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire BLE <- Sensor
    mBle->input.SensorDataEvents = mSensor->output.DataEvents;

    // Wire Command <- Sensor
    mCommand->input.Sensor = mSensor->output.Sensor;

    ESP_LOGI(TAG, "Services wired");
}

void Controller::initHAL() {
    // Phase 1: Hardware initialization (order matters)
    ESP_ERROR_CHECK(mTimer->init_HAL());
    ESP_ERROR_CHECK(mSensor->init_HAL());
    ESP_ERROR_CHECK(mBle->init_HAL());
    ESP_ERROR_CHECK(mLed->init_HAL());
    ESP_ERROR_CHECK(mMqtt->init_HAL());

    ESP_LOGI(TAG, "HAL initialized");
}

void Controller::initServices() {
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

    // MQTT init (no dependencies)
    ESP_ERROR_CHECK(mMqtt->init());

    ESP_LOGI(TAG, "Services initialized");
}

void Controller::startServices() {
    ESP_ERROR_CHECK(mTimer->start());
    ESP_ERROR_CHECK(mSensor->start());
    ESP_ERROR_CHECK(mBle->start());
    ESP_ERROR_CHECK(mCommand->Start());
    ESP_ERROR_CHECK(mLed->start());
    ESP_ERROR_CHECK(mMqtt->start());

    ESP_LOGI(TAG, "Services started");
}

} // namespace Arcana
