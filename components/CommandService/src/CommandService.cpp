#include "CommandService.hpp"
#include "CryptoEngine.hpp"
#include "esp_log.h"

static const char* TAG = "CommandService";

namespace Arcana {
namespace Command {

CommandService& CommandService::Instance() {
    static CommandService sInstance;
    return sInstance;
}

esp_err_t CommandService::init() {
    return Init(input.Sensor);
}

esp_err_t CommandService::Init(Sensor::ObservableSensor* sensor) {
#ifdef CONFIG_CMD_ENCRYPTION_ENABLED
    // Init KeyExchangeManager with PSK
    uint8_t psk[CryptoEngine::kKeyLen];
    if (CryptoEngine::HexToKey(CONFIG_CMD_ENCRYPTION_PSK, psk)) {
        mKeyExchangeMgr = std::make_unique<KeyExchangeManager>();
        esp_err_t err = mKeyExchangeMgr->Init(psk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "KeyExchangeManager init failed");
            mKeyExchangeMgr.reset();
        } else {
            ESP_LOGI(TAG, "KeyExchangeManager initialized");
        }
    }
#endif

    CommandFactory::Dependencies deps;
    deps.Sensor = sensor;
    deps.KeyExchangeMgr = mKeyExchangeMgr.get();

    mFactory = std::make_unique<CommandFactory>(deps);
    mDispatcher = std::make_unique<CommandDispatcher>(*mFactory);

    esp_err_t ret = mDispatcher->Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Dispatcher init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Populate output pointers for Controller wiring
    output.ResponseEvents = &mDispatcher->ResponseEvents();
    output.KeyExchangeMgr = mKeyExchangeMgr.get();
    output.Factory = mFactory.get();

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t CommandService::Start() {
    if (!mDispatcher) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = mDispatcher->Start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Dispatcher start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void CommandService::Stop() {
    if (mDispatcher) {
        mDispatcher->Stop();
    }
    ESP_LOGI(TAG, "Stopped");
}

void CommandService::HandleRequest(const CommandRequest& request) {
    if (mDispatcher) {
        mDispatcher->Dispatch(request);
    }
}

Observable<CommandResponse>& CommandService::ResponseEvents() {
    return mDispatcher->ResponseEvents();
}

} // namespace Command
} // namespace Arcana
