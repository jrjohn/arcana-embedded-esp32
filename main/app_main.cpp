/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "BleService.hpp"
#include "ObservableSensor.hpp"
#include "CommandService.hpp"
#include "BleCommandAdapter.hpp"
#include "MqttCommandAdapter.hpp"
#include "commands/GetMqttStatusCommand.hpp"

static const char *TAG = "mqtt5_example";

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static esp_mqtt5_user_property_item_t user_property_arr[] = {
        {"board", "esp32"},
        {"u", "user"},
        {"p", "password"}
    };

#define USE_PROPERTY_ARR_SIZE   sizeof(user_property_arr)/sizeof(esp_mqtt5_user_property_item_t)

static esp_mqtt5_publish_property_config_t publish_property = {};
static esp_mqtt5_subscribe_property_config_t subscribe_property = {};
static esp_mqtt5_subscribe_property_config_t subscribe1_property = {};
static esp_mqtt5_unsubscribe_property_config_t unsubscribe_property = {};
static esp_mqtt5_disconnect_property_config_t disconnect_property = {};

static void init_mqtt5_properties(void)
{
    publish_property.payload_format_indicator = 1;
    publish_property.message_expiry_interval = 1000;
    publish_property.topic_alias = 0;
    publish_property.response_topic = "/topic/test/response";
    publish_property.correlation_data = "123456";
    publish_property.correlation_data_len = 6;

    subscribe_property.subscribe_id = 25555;
    subscribe_property.no_local_flag = false;
    subscribe_property.retain_as_published_flag = false;
    subscribe_property.retain_handle = 0;
    subscribe_property.is_share_subscribe = true;
    subscribe_property.share_name = "group1";

    subscribe1_property.subscribe_id = 25555;
    subscribe1_property.no_local_flag = true;
    subscribe1_property.retain_as_published_flag = false;
    subscribe1_property.retain_handle = 0;

    unsubscribe_property.is_share_subscribe = true;
    unsubscribe_property.share_name = "group1";

    disconnect_property.session_expiry_interval = 60;
    disconnect_property.disconnect_reason = 0;
}

static void print_user_property(mqtt5_user_property_handle_t user_property)
{
    if (user_property) {
        uint8_t count = esp_mqtt5_client_get_user_property_count(user_property);
        if (count) {
            auto *item = static_cast<esp_mqtt5_user_property_item_t*>(
                malloc(count * sizeof(esp_mqtt5_user_property_item_t)));
            if (esp_mqtt5_client_get_user_property(user_property, item, &count) == ESP_OK) {
                for (int i = 0; i < count; i ++) {
                    esp_mqtt5_user_property_item_t *t = &item[i];
                    ESP_LOGI(TAG, "key is %s, value is %s", t->key, t->value);
                    free((char *)t->key);
                    free((char *)t->value);
                }
            }
            free(item);
        }
    }
}

// MQTT client handle (needed for publishing responses)
static esp_mqtt_client_handle_t sMqttClient = nullptr;

// Command topic
#ifdef CONFIG_CMD_MQTT_CMD_TOPIC
static const char* sCmdTopic = CONFIG_CMD_MQTT_CMD_TOPIC;
#else
static const char* sCmdTopic = "arcana/cmd";
#endif

#ifdef CONFIG_CMD_MQTT_RSP_TOPIC
static const char* sRspTopic = CONFIG_CMD_MQTT_RSP_TOPIC;
#else
static const char* sRspTopic = "arcana/rsp";
#endif

/*
 * @brief Event handler registered to receive MQTT events
 */
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        print_user_property(event->property->user_property);
        esp_mqtt5_client_set_user_property(&publish_property.user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_publish_property(client, &publish_property);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 1);
        esp_mqtt5_client_delete_user_property(publish_property.user_property);
        publish_property.user_property = NULL;
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        esp_mqtt5_client_set_user_property(&subscribe_property.user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_subscribe_property(client, &subscribe_property);
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        esp_mqtt5_client_delete_user_property(subscribe_property.user_property);
        subscribe_property.user_property = NULL;
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        esp_mqtt5_client_set_user_property(&subscribe1_property.user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_subscribe_property(client, &subscribe1_property);
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 2);
        esp_mqtt5_client_delete_user_property(subscribe1_property.user_property);
        subscribe1_property.user_property = NULL;
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // Subscribe to command topic
        msg_id = esp_mqtt_client_subscribe(client, sCmdTopic, 1);
        ESP_LOGI(TAG, "Subscribed to command topic '%s', msg_id=%d", sCmdTopic, msg_id);

        // Update MQTT status in command service
        {
            auto* factory = Arcana::Command::CommandService::Instance().Factory();
            if (factory && factory->MqttStatusCmd()) {
                factory->MqttStatusCmd()->SetConnected(true);
            }
        }

        esp_mqtt5_client_set_user_property(&unsubscribe_property.user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_unsubscribe_property(client, &unsubscribe_property);
        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos0");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        esp_mqtt5_client_delete_user_property(unsubscribe_property.user_property);
        unsubscribe_property.user_property = NULL;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        print_user_property(event->property->user_property);
        {
            auto* factory = Arcana::Command::CommandService::Instance().Factory();
            if (factory && factory->MqttStatusCmd()) {
                factory->MqttStatusCmd()->SetConnected(false);
            }
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        print_user_property(event->property->user_property);
        esp_mqtt5_client_set_publish_property(client, &publish_property);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        print_user_property(event->property->user_property);
        esp_mqtt5_client_set_user_property(&disconnect_property.user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_disconnect_property(client, &disconnect_property);
        esp_mqtt5_client_delete_user_property(disconnect_property.user_property);
        disconnect_property.user_property = NULL;
        esp_mqtt_client_disconnect(client);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        print_user_property(event->property->user_property);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        print_user_property(event->property->user_property);
        ESP_LOGI(TAG, "payload_format_indicator is %d", event->property->payload_format_indicator);
        ESP_LOGI(TAG, "response_topic is %.*s", event->property->response_topic_len, event->property->response_topic);
        ESP_LOGI(TAG, "correlation_data is %.*s", event->property->correlation_data_len, event->property->correlation_data);
        ESP_LOGI(TAG, "content_type is %.*s", event->property->content_type_len, event->property->content_type);
        ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);

        // Check if this is a command message
        if (event->topic_len > 0 && event->data_len > 0 &&
            strncmp(event->topic, sCmdTopic, event->topic_len) == 0 &&
            static_cast<size_t>(event->topic_len) == strlen(sCmdTopic)) {
            Arcana::Command::CommandRequest req;
            if (Arcana::Command::MqttCommandAdapter::ParseRequest(
                    event->data, event->data_len, req)) {
                ESP_LOGI(TAG, "MQTT command received: func=0x%02x",
                         static_cast<uint8_t>(req.Function));
                Arcana::Command::CommandService::Instance().HandleRequest(req);
            } else {
                ESP_LOGW(TAG, "Failed to parse MQTT command");
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        print_user_property(event->property->user_property);
        ESP_LOGI(TAG, "MQTT5 return code is %d", event->error_handle->connect_return_code);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt5_app_start(void)
{
    init_mqtt5_properties();

    esp_mqtt5_connection_property_config_t connect_property = {};
    connect_property.session_expiry_interval = 10;
    connect_property.maximum_packet_size = 1024;
    connect_property.receive_maximum = 65535;
    connect_property.topic_alias_maximum = 2;
    connect_property.request_resp_info = true;
    connect_property.request_problem_info = true;
    connect_property.will_delay_interval = 10;
    connect_property.payload_format_indicator = true;
    connect_property.message_expiry_interval = 10;
    connect_property.response_topic = "/test/response";
    connect_property.correlation_data = "123456";
    connect_property.correlation_data_len = 6;

    esp_mqtt_client_config_t mqtt5_cfg = {};
    mqtt5_cfg.broker.address.uri = CONFIG_BROKER_URL;
    mqtt5_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
    mqtt5_cfg.network.disable_auto_reconnect = true;
    mqtt5_cfg.credentials.username = "123";
    mqtt5_cfg.credentials.authentication.password = "456";
    mqtt5_cfg.session.last_will.topic = "/topic/will";
    mqtt5_cfg.session.last_will.msg = "i will leave";
    mqtt5_cfg.session.last_will.msg_len = 12;
    mqtt5_cfg.session.last_will.qos = 1;
    mqtt5_cfg.session.last_will.retain = true;

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt5_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt5_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    sMqttClient = esp_mqtt_client_init(&mqtt5_cfg);

    /* Set connection properties and user properties */
    esp_mqtt5_client_set_user_property(&connect_property.user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_user_property(&connect_property.will_user_property, user_property_arr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_connect_property(sMqttClient, &connect_property);

    esp_mqtt5_client_delete_user_property(connect_property.user_property);
    esp_mqtt5_client_delete_user_property(connect_property.will_user_property);

    esp_mqtt_client_register_event(sMqttClient, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt5_event_handler, NULL);
    esp_mqtt_client_start(sMqttClient);
}

/*******************************************************************************
 * BLE ← ObservableSensor Bridge
 ******************************************************************************/

static Arcana::Sensor::ObservableSensor* sSensor = nullptr;

static void ble_sensor_bridge_init(void)
{
    using namespace Arcana::Sensor;
    using namespace Arcana::Ble;

    // Create a simulated sensor (replace ReadHardware() for real hardware)
    static ObservableSensor sensor(SensorConfig().WithId(1).WithInterval(
#ifdef CONFIG_BLE_NOTIFY_INTERVAL_MS
        CONFIG_BLE_NOTIFY_INTERVAL_MS
#else
        1000
#endif
    ));
    sSensor = &sensor;

    // Bridge: ObservableSensor data → BLE GATT Server notifications
    sensor.OnData([](const SensorData& data) {
        auto& server = BleGattServer::Instance();

        // SensorData.Temperature is float in Celsius → BLE uses int16 (Celsius * 100)
        int16_t tempCenti = static_cast<int16_t>(data.Temperature * 100.0f);
        server.UpdateTemperature(tempCenti);

        // SensorData.Humidity is float in % → BLE uses uint16 (% * 100)
        uint16_t humidCenti = static_cast<uint16_t>(data.Humidity * 100.0f);
        server.UpdateHumidity(humidCenti);

        ESP_LOGD(TAG, "BLE notify: temp=%d humid=%u", tempCenti, humidCenti);
    });

    sensor.Start();
    ESP_LOGI(TAG, "Sensor→BLE bridge started");
}

/*******************************************************************************
 * Command Service Wiring
 ******************************************************************************/

static void command_service_init(void)
{
    using namespace Arcana::Command;
    using namespace Arcana::Ble;

    auto& cmdSvc = CommandService::Instance();
    ESP_ERROR_CHECK(cmdSvc.Init(sSensor));
    ESP_ERROR_CHECK(cmdSvc.Start());

    // Wire BLE Command writes → CommandService
    BleGattServer::Instance().CommandWriteEvents().Subscribe(
        [](const BleCommandWriteEvent& evt) {
            CommandRequest req;
            if (BleCommandAdapter::ParseRequest(evt.first, evt.second.data(),
                                                 static_cast<uint16_t>(evt.second.size()), req)) {
                ESP_LOGI(TAG, "BLE command received: func=0x%02x conn_id=%d",
                         static_cast<uint8_t>(req.Function), req.ConnectionId);
                CommandService::Instance().HandleRequest(req);
            } else {
                ESP_LOGW(TAG, "Failed to parse BLE command");
            }
        }
    );

    // Wire CommandService responses → BLE / MQTT
    cmdSvc.ResponseEvents().Subscribe(
        [](const CommandResponse& rsp) {
            switch (rsp.Source) {
            case CommandSource::BLE: {
                uint8_t buf[264];
                uint16_t outLen = 0;
                if (BleCommandAdapter::SerializeResponse(rsp, buf, sizeof(buf), outLen)) {
                    BleGattServer::Instance().SendCommandResponse(rsp.ConnectionId, buf, outLen);
                    ESP_LOGI(TAG, "BLE response sent: func=0x%02x status=%d len=%d",
                             static_cast<uint8_t>(rsp.Function), rsp.Status, outLen);
                }
                break;
            }
            case CommandSource::MQTT: {
                if (sMqttClient) {
                    char jsonBuf[512];
                    size_t jsonLen = 0;
                    if (MqttCommandAdapter::SerializeResponse(rsp, jsonBuf, sizeof(jsonBuf), jsonLen)) {
                        esp_mqtt_client_publish(sMqttClient, sRspTopic, jsonBuf,
                                                static_cast<int>(jsonLen), 1, 0);
                        ESP_LOGI(TAG, "MQTT response sent: %.*s", (int)jsonLen, jsonBuf);
                    }
                }
                break;
            }
            default:
                ESP_LOGD(TAG, "Internal command response: func=0x%02x",
                         static_cast<uint8_t>(rsp.Function));
                break;
            }
        }
    );

    ESP_LOGI(TAG, "Command service wired to BLE + MQTT");
}

/*******************************************************************************
 * app_main
 ******************************************************************************/

extern "C" void app_main(void)
{
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

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize BLE */
    ESP_ERROR_CHECK(Arcana::Ble::BleService::Instance().Init());
    ESP_ERROR_CHECK(Arcana::Ble::BleService::Instance().Start());

    /* Bridge ObservableSensor → BLE GATT Server */
    ble_sensor_bridge_init();

    /* Initialize Command Service */
    command_service_init();

    /* Wi-Fi + MQTT5 */
    ESP_ERROR_CHECK(example_connect());
    mqtt5_app_start();
}
