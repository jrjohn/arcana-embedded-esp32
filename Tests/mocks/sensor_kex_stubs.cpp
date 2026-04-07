// Stub implementations for ObservableSensor and KeyExchangeManager methods
// referenced by Command headers (commands/GetSensorData/SetNotifyInterval/
// KeyExchange). Tests pass nullptr for the Sensor* and KeyExchangeMgr*, so
// these stubs are never actually invoked at runtime — but the linker needs
// the symbols to exist.

#include "ObservableSensor.hpp"
#include "KeyExchangeManager.hpp"
#include "SensorTypes.hpp"

namespace Arcana {
namespace Sensor {

ObservableSensor::ObservableSensor(const SensorConfig& Config)
    : mConfig(Config)
    , mTaskHandle(nullptr)
    , mRunning(false)
    , mLastThresholdState(0) {
    mDataMutex = xSemaphoreCreateMutex();
}

ObservableSensor::~ObservableSensor() {
    if (mDataMutex) vSemaphoreDelete(mDataMutex);
}

esp_err_t ObservableSensor::Start() { return ESP_OK; }
esp_err_t ObservableSensor::Stop() { return ESP_OK; }

void ObservableSensor::SetConfig(const SensorConfig& Config) { mConfig = Config; }
SensorConfig ObservableSensor::GetConfig() const { return mConfig; }

esp_err_t ObservableSensor::ReadSync(SensorData& /*Data*/) { return ESP_OK; }
SensorData ObservableSensor::GetLastReading() const { return mLastData; }

esp_err_t ObservableSensor::ReadHardware(SensorData& /*Data*/) { return ESP_OK; }

void ObservableSensor::TaskEntry(void*) {}
void ObservableSensor::TaskLoop() {}
void ObservableSensor::CheckThresholds(const SensorData&) {}
void ObservableSensor::EmitLifecycle(LifecycleEvent::State) {}
void ObservableSensor::NotifyAny(const IModel*) {}

SubscriptionId ObservableSensor::OnData(Observer<SensorData>) { return 0; }
SubscriptionId ObservableSensor::OnError(Observer<SensorError>) { return 0; }
SubscriptionId ObservableSensor::OnThreshold(Observer<ThresholdEvent>) { return 0; }
SubscriptionId ObservableSensor::OnLifecycle(Observer<LifecycleEvent>) { return 0; }
SubscriptionId ObservableSensor::OnAny(Observer<const IModel*>) { return 0; }

bool ObservableSensor::UnsubscribeData(SubscriptionId) { return false; }
bool ObservableSensor::UnsubscribeError(SubscriptionId) { return false; }
bool ObservableSensor::UnsubscribeThreshold(SubscriptionId) { return false; }
bool ObservableSensor::UnsubscribeLifecycle(SubscriptionId) { return false; }
bool ObservableSensor::UnsubscribeAny(SubscriptionId) { return false; }

Subscription<SensorData>      ObservableSensor::SubscribeData(Observer<SensorData> cb)            { return Subscription<SensorData>(mDataObservable, mDataObservable.Subscribe(std::move(cb))); }
Subscription<SensorError>     ObservableSensor::SubscribeError(Observer<SensorError> cb)          { return Subscription<SensorError>(mErrorObservable, mErrorObservable.Subscribe(std::move(cb))); }
Subscription<ThresholdEvent>  ObservableSensor::SubscribeThreshold(Observer<ThresholdEvent> cb)   { return Subscription<ThresholdEvent>(mThresholdObservable, mThresholdObservable.Subscribe(std::move(cb))); }
Subscription<LifecycleEvent>  ObservableSensor::SubscribeLifecycle(Observer<LifecycleEvent> cb)   { return Subscription<LifecycleEvent>(mLifecycleObservable, mLifecycleObservable.Subscribe(std::move(cb))); }
Subscription<const IModel*>   ObservableSensor::SubscribeAny(Observer<const IModel*> cb)          { return Subscription<const IModel*>(mAnyObservable, mAnyObservable.Subscribe(std::move(cb))); }

} // namespace Sensor
} // namespace Arcana

namespace Arcana {
namespace Command {

KeyExchangeManager::KeyExchangeManager() {}
KeyExchangeManager::~KeyExchangeManager() {}

esp_err_t KeyExchangeManager::Init(const uint8_t /*psk*/[CryptoEngine::kKeyLen]) { return ESP_OK; }

bool KeyExchangeManager::PerformKeyExchange(CommandSource /*source*/, uint16_t /*connId*/,
                                              const uint8_t /*clientPub*/[64],
                                              uint8_t /*serverPub*/[64], uint8_t /*authTag*/[32]) {
    return false;  // tests pass nullptr → never called
}

bool KeyExchangeManager::InstallPendingSession(CommandSource, uint16_t) { return false; }
void KeyExchangeManager::RemoveSession(CommandSource, uint16_t) {}
CryptoEngine* KeyExchangeManager::GetSession(CommandSource, uint16_t) { return nullptr; }

bool KeyExchangeManager::DecryptWithSession(CommandSource, uint16_t,
                                              const uint8_t*, size_t,
                                              uint8_t*, size_t, size_t&) {
    return false;
}
bool KeyExchangeManager::EncryptWithSession(CommandSource, uint16_t,
                                              const uint8_t*, size_t,
                                              uint8_t*, size_t, size_t&) {
    return false;
}

} // namespace Command
} // namespace Arcana
