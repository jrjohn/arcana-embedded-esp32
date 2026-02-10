#include "MqttTransportServiceImpl.hpp"
#include "esp_log.h"
#include <cstring>
#include <cstdio>
#include <cinttypes>

static const char* TAG = "MqttService";

// MQTT5 property configs
static esp_mqtt5_user_property_item_t sUserPropertyArr[] = {
    {"board", "esp32"},
    {"u", "user"},
    {"p", "password"}
};
#define USE_PROPERTY_ARR_SIZE (sizeof(sUserPropertyArr) / sizeof(esp_mqtt5_user_property_item_t))

static esp_mqtt5_publish_property_config_t sPublishProperty = {};
static esp_mqtt5_subscribe_property_config_t sSubscribeProperty = {};
static esp_mqtt5_subscribe_property_config_t sSubscribe1Property = {};
static esp_mqtt5_unsubscribe_property_config_t sUnsubscribeProperty = {};
static esp_mqtt5_disconnect_property_config_t sDisconnectProperty = {};

static void init_mqtt5_properties(void)
{
    sPublishProperty.payload_format_indicator = 1;
    sPublishProperty.message_expiry_interval = 1000;
    sPublishProperty.topic_alias = 0;
    sPublishProperty.response_topic = "/topic/test/response";
    sPublishProperty.correlation_data = "123456";
    sPublishProperty.correlation_data_len = 6;

    sSubscribeProperty.subscribe_id = 25555;
    sSubscribeProperty.no_local_flag = false;
    sSubscribeProperty.retain_as_published_flag = false;
    sSubscribeProperty.retain_handle = 0;
    sSubscribeProperty.is_share_subscribe = true;
    sSubscribeProperty.share_name = "group1";

    sSubscribe1Property.subscribe_id = 25555;
    sSubscribe1Property.no_local_flag = true;
    sSubscribe1Property.retain_as_published_flag = false;
    sSubscribe1Property.retain_handle = 0;

    sUnsubscribeProperty.is_share_subscribe = true;
    sUnsubscribeProperty.share_name = "group1";

    sDisconnectProperty.session_expiry_interval = 60;
    sDisconnectProperty.disconnect_reason = 0;
}

static void print_user_property(mqtt5_user_property_handle_t user_property)
{
    if (user_property) {
        uint8_t count = esp_mqtt5_client_get_user_property_count(user_property);
        if (count) {
            auto *item = static_cast<esp_mqtt5_user_property_item_t*>(
                malloc(count * sizeof(esp_mqtt5_user_property_item_t)));
            if (esp_mqtt5_client_get_user_property(user_property, item, &count) == ESP_OK) {
                for (int i = 0; i < count; i++) {
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

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

namespace Arcana {
namespace Mqtt {

MqttTransportServiceImpl::MqttTransportServiceImpl() {
    output.CommandEvents = new Observable<MqttCommandEvent>("MqttSvc CommandEvents");
    output.ConnectionStatus = new Observable<MqttConnectionStatus>("MqttSvc ConnStatus");

    ESP_LOGI(TAG, "Created (output Observables allocated)");
}

MqttTransportServiceImpl::~MqttTransportServiceImpl() {
    if (output.CommandEvents) {
        delete output.CommandEvents;
        output.CommandEvents = nullptr;
    }
    if (output.ConnectionStatus) {
        delete output.ConnectionStatus;
        output.ConnectionStatus = nullptr;
    }
}

MqttTransportService& MqttTransportServiceImpl::getInstance() {
    static MqttTransportServiceImpl sInstance;
    return sInstance;
}

esp_err_t MqttTransportServiceImpl::init_HAL() {
    // Command topic from Kconfig
#ifdef CONFIG_MQTT_SVC_CMD_TOPIC
    mCmdTopic = CONFIG_MQTT_SVC_CMD_TOPIC;
#else
    mCmdTopic = "arcana/cmd";
#endif

    ESP_LOGI(TAG, "HAL initialized");
    return ESP_OK;
}

esp_err_t MqttTransportServiceImpl::init() {
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t MqttTransportServiceImpl::start() {
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

    mClient = esp_mqtt_client_init(&mqtt5_cfg);
    if (!mClient) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ESP_FAIL;
    }

    esp_mqtt5_client_set_user_property(&connect_property.user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_user_property(&connect_property.will_user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_connect_property(mClient, &connect_property);

    esp_mqtt5_client_delete_user_property(connect_property.user_property);
    esp_mqtt5_client_delete_user_property(connect_property.will_user_property);

    esp_mqtt_client_register_event(mClient,
        static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
        eventHandler, this);

    esp_err_t err = esp_mqtt_client_start(mClient);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Started (broker=%s)", CONFIG_BROKER_URL);
    return ESP_OK;
}

void MqttTransportServiceImpl::stop() {
    if (mClient) {
        esp_mqtt_client_stop(mClient);
        esp_mqtt_client_destroy(mClient);
        mClient = nullptr;
    }
    ESP_LOGI(TAG, "Stopped");
}

esp_err_t MqttTransportServiceImpl::publish(const char* topic, const uint8_t* data, size_t len, int qos) {
    if (!mClient) return ESP_ERR_INVALID_STATE;

    int msgId = esp_mqtt_client_publish(mClient, topic,
        reinterpret_cast<const char*>(data), static_cast<int>(len), qos, 0);
    if (msgId < 0) return ESP_FAIL;

    return ESP_OK;
}

void MqttTransportServiceImpl::eventHandler(void* handlerArgs, esp_event_base_t base,
                                             int32_t eventId, void* eventData) {
    auto* self = static_cast<MqttTransportServiceImpl*>(handlerArgs);
    auto* event = static_cast<esp_mqtt_event_handle_t>(eventData);
    self->handleEvent(event);
}

void MqttTransportServiceImpl::handleEvent(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (static_cast<esp_mqtt_event_id_t>(event->event_id)) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        print_user_property(event->property->user_property);

        esp_mqtt5_client_set_user_property(&sPublishProperty.user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_publish_property(client, &sPublishProperty);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 1);
        esp_mqtt5_client_delete_user_property(sPublishProperty.user_property);
        sPublishProperty.user_property = NULL;
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        esp_mqtt5_client_set_user_property(&sSubscribeProperty.user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_subscribe_property(client, &sSubscribeProperty);
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        esp_mqtt5_client_delete_user_property(sSubscribeProperty.user_property);
        sSubscribeProperty.user_property = NULL;
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        esp_mqtt5_client_set_user_property(&sSubscribe1Property.user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_subscribe_property(client, &sSubscribe1Property);
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 2);
        esp_mqtt5_client_delete_user_property(sSubscribe1Property.user_property);
        sSubscribe1Property.user_property = NULL;
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // Subscribe to command topic
        msg_id = esp_mqtt_client_subscribe(client, mCmdTopic, 1);
        ESP_LOGI(TAG, "Subscribed to command topic '%s', msg_id=%d", mCmdTopic, msg_id);

        // Notify connection status
        output.ConnectionStatus->Notify(MqttConnectionStatus{true});

        esp_mqtt5_client_set_user_property(&sUnsubscribeProperty.user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_unsubscribe_property(client, &sUnsubscribeProperty);
        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos0");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        esp_mqtt5_client_delete_user_property(sUnsubscribeProperty.user_property);
        sUnsubscribeProperty.user_property = NULL;
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        print_user_property(event->property->user_property);
        output.ConnectionStatus->Notify(MqttConnectionStatus{false});
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        print_user_property(event->property->user_property);
        esp_mqtt5_client_set_publish_property(client, &sPublishProperty);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        print_user_property(event->property->user_property);
        esp_mqtt5_client_set_user_property(&sDisconnectProperty.user_property, sUserPropertyArr, USE_PROPERTY_ARR_SIZE);
        esp_mqtt5_client_set_disconnect_property(client, &sDisconnectProperty);
        esp_mqtt5_client_delete_user_property(sDisconnectProperty.user_property);
        sDisconnectProperty.user_property = NULL;
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
            strncmp(event->topic, mCmdTopic, event->topic_len) == 0 &&
            static_cast<size_t>(event->topic_len) == strlen(mCmdTopic)) {
            MqttCommandEvent cmdEvt;
            cmdEvt.Data = reinterpret_cast<const uint8_t*>(event->data);
            cmdEvt.Len = static_cast<size_t>(event->data_len);
            output.CommandEvents->Notify(cmdEvt);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        print_user_property(event->property->user_property);
        ESP_LOGI(TAG, "MQTT5 return code is %d", event->error_handle->connect_return_code);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

} // namespace Mqtt
} // namespace Arcana
