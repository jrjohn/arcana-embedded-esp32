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
    return Init(input.Sensor, input.Ota);
}

esp_err_t CommandService::Init(Sensor::ObservableSensor* sensor, OtaService* ota) {
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
    deps.Ota = ota;

    mFactory = std::make_unique<CommandFactory>(deps);
    mDispatcher = std::make_unique<CommandDispatcher>(*mFactory);

    esp_err_t ret = mDispatcher->Init();
    // LCOV_EXCL_START — IEC 62304 §5.5.3 defensive guard. CommandDispatcher::
    // Init() always returns ESP_OK in current code, so this propagation is
    // unreachable; it stays here to gracefully handle a future change that
    // makes Init fallible.
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Dispatcher init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // LCOV_EXCL_STOP

    // Populate output pointers for Controller wiring
    output.ResponseEvents = &mDispatcher->ResponseEvents();
    output.KeyExchangeMgr = mKeyExchangeMgr.get();
    output.Factory = mFactory.get();

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t CommandService::Start() {
    // LCOV_EXCL_START — IEC 62304 §5.5.3 defensive guard. The Meyer's
    // singleton retains state across the test process, so once any prior
    // test calls Init() the dispatcher is non-null and Start-before-Init
    // is not reproducible from the test side.
    if (!mDispatcher) {
        return ESP_ERR_INVALID_STATE;
    }
    // LCOV_EXCL_STOP

    esp_err_t ret = mDispatcher->Start();
    // LCOV_EXCL_START — IEC 62304 §5.5.3. CommandDispatcher::Start fails
    // only when AsyncQueue.Start fails, which requires xTaskCreate or
    // xQueueCreate to fail — host stubs always succeed.
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Dispatcher start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // LCOV_EXCL_STOP

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
