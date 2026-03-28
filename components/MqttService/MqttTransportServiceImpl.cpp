#include "MqttTransportServiceImpl.hpp"
#include "esp_log.h"
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <ctime>

static const char* TAG = "MqttService";

namespace Arcana {
namespace Mqtt {

namespace {
void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}
} // anonymous namespace

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
    if (input.SensorDataEvents) {
        input.SensorDataEvents->Subscribe([this](const Sensor::SensorData& data) {
            char json[128];
            int len = snprintf(json, sizeof(json),
                "{\"temperature\":%.1f,\"humidity\":%.1f,\"timestamp\":%lld}",
                data.Temperature, data.Humidity, (long long)time(nullptr));
            publish("arcana/sensor",
                reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(len), 1);
        });
    }

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t MqttTransportServiceImpl::start() {
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = CONFIG_BROKER_URL;
    cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
    cfg.network.disable_auto_reconnect = false;

    mClient = esp_mqtt_client_init(&cfg);
    if (!mClient) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ESP_FAIL;
    }

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
    switch (static_cast<esp_mqtt_event_id_t>(event->event_id)) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        // Subscribe to command topic
        int msg_id = esp_mqtt_client_subscribe(event->client, mCmdTopic, 1);
        ESP_LOGI(TAG, "Subscribed to command topic '%s', msg_id=%d", mCmdTopic, msg_id);

        // Notify connection status
        output.ConnectionStatus->Notify(MqttConnectionStatus{true});
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        output.ConnectionStatus->Notify(MqttConnectionStatus{false});
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA TOPIC=%.*s", event->topic_len, event->topic);

        // Check if this is a command message
        if (event->topic_len > 0 && event->data_len > 0 &&
            strncmp(event->topic, mCmdTopic, event->topic_len) == 0 &&
            static_cast<size_t>(event->topic_len) == strlen(mCmdTopic)) {
            MqttCommandEvent cmdEvt{};
            size_t dataLen = static_cast<size_t>(event->data_len);
            if (dataLen > MqttCommandEvent::kMaxDataLen) {
                ESP_LOGW(TAG, "MQTT cmd data too large: %zu > %zu, dropping",
                         dataLen, MqttCommandEvent::kMaxDataLen);
                break;
            }
            memcpy(cmdEvt.Data, event->data, dataLen);
            cmdEvt.Len = dataLen;
            output.CommandEvents->Notify(cmdEvt);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGE(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

} // namespace Mqtt
} // namespace Arcana
