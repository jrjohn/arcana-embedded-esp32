#pragma once

#include "MqttTransportService.hpp"

namespace Arcana {
namespace Mqtt {

class MqttTransportServiceImpl : public MqttTransportService {
public:
    static MqttTransportService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    esp_err_t publish(const char* topic, const uint8_t* data, size_t len, int qos = 1) override;
    esp_mqtt_client_handle_t clientHandle() override { return mClient; }

private:
    MqttTransportServiceImpl();
    ~MqttTransportServiceImpl() override;
    MqttTransportServiceImpl(const MqttTransportServiceImpl&) = delete;
    MqttTransportServiceImpl& operator=(const MqttTransportServiceImpl&) = delete;

    static void eventHandler(void* handlerArgs, esp_event_base_t base,
                             int32_t eventId, void* eventData);
    void handleEvent(esp_mqtt_event_handle_t event);

    esp_mqtt_client_handle_t mClient = nullptr;
    const char* mCmdTopic = nullptr;
};

} // namespace Mqtt
} // namespace Arcana
