/*
 * Observable Sensor Component - Implementation
 *
 * C++ RTOS + Observable pattern for ESP32
 */

#include "ObservableSensor.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <cstring>

static const char* TAG = "ObservableSensor";

namespace Arcana {
namespace Sensor {

/*******************************************************************************
 * Constructor / Destructor
 ******************************************************************************/

ObservableSensor::ObservableSensor(const SensorConfig& Config)
    : mConfig(Config)
    , mTaskHandle(nullptr)
    , mRunning(false)
    , mShouldStop(false)
    , mLastThresholdState(0)
{
    mConfigMutex = xSemaphoreCreateMutex();
    mDataMutex = xSemaphoreCreateMutex();

    configASSERT(mConfigMutex != nullptr);
    configASSERT(mDataMutex != nullptr);

    ESP_LOGI(TAG, "Sensor %d initialized (interval=%ums)",
             mConfig.SensorId, mConfig.ReadIntervalMs);

    EmitLifecycle(LifecycleEvent::State::Initialized);
}

ObservableSensor::~ObservableSensor() {
    if (mRunning.load()) {
        Stop();
    }

    EmitLifecycle(LifecycleEvent::State::Deinitialized);

    if (mConfigMutex) {
        vSemaphoreDelete(mConfigMutex);
    }
    if (mDataMutex) {
        vSemaphoreDelete(mDataMutex);
    }

    ESP_LOGI(TAG, "Sensor %d destroyed", mConfig.SensorId);
}

ObservableSensor::ObservableSensor(ObservableSensor&& Other) noexcept
    : mConfig(Other.mConfig)
    , mConfigMutex(Other.mConfigMutex)
    , mTaskHandle(Other.mTaskHandle)
    , mRunning(Other.mRunning.load())
    , mShouldStop(Other.mShouldStop.load())
    , mLastData(Other.mLastData)
    , mDataMutex(Other.mDataMutex)
    , mLastThresholdState(Other.mLastThresholdState)
    , mDataObservable(std::move(Other.mDataObservable))
    , mErrorObservable(std::move(Other.mErrorObservable))
    , mThresholdObservable(std::move(Other.mThresholdObservable))
    , mLifecycleObservable(std::move(Other.mLifecycleObservable))
    , mAnyObservable(std::move(Other.mAnyObservable))
{
    Other.mConfigMutex = nullptr;
    Other.mDataMutex = nullptr;
    Other.mTaskHandle = nullptr;
}

ObservableSensor& ObservableSensor::operator=(ObservableSensor&& Other) noexcept {
    if (this != &Other) {
        if (mRunning.load()) {
            Stop();
        }
        if (mConfigMutex) vSemaphoreDelete(mConfigMutex);
        if (mDataMutex) vSemaphoreDelete(mDataMutex);

        mConfig = Other.mConfig;
        mConfigMutex = Other.mConfigMutex;
        mTaskHandle = Other.mTaskHandle;
        mRunning.store(Other.mRunning.load());
        mShouldStop.store(Other.mShouldStop.load());
        mLastData = Other.mLastData;
        mDataMutex = Other.mDataMutex;
        mLastThresholdState = Other.mLastThresholdState;
        mDataObservable = std::move(Other.mDataObservable);
        mErrorObservable = std::move(Other.mErrorObservable);
        mThresholdObservable = std::move(Other.mThresholdObservable);
        mLifecycleObservable = std::move(Other.mLifecycleObservable);
        mAnyObservable = std::move(Other.mAnyObservable);

        Other.mConfigMutex = nullptr;
        Other.mDataMutex = nullptr;
        Other.mTaskHandle = nullptr;
    }
    return *this;
}

/*******************************************************************************
 * Lifecycle Management
 ******************************************************************************/

esp_err_t ObservableSensor::Start() {
    if (mRunning.load()) {
        ESP_LOGW(TAG, "Sensor %d already running", mConfig.SensorId);
        return ESP_ERR_INVALID_STATE;
    }

    mShouldStop.store(false);
    mRunning.store(true);

    char TaskName[16];
    snprintf(TaskName, sizeof(TaskName), "sensor_%d", mConfig.SensorId);

    BaseType_t Ret = xTaskCreate(
        TaskEntry,
        TaskName,
        mConfig.TaskStackSize,
        this,
        mConfig.TaskPriority,
        &mTaskHandle
    );

    if (Ret != pdPASS) {
        mRunning.store(false);
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sensor %d started", mConfig.SensorId);
    return ESP_OK;
}

esp_err_t ObservableSensor::Stop() {
    if (!mRunning.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    mShouldStop.store(true);

    // Wait for task to finish (max 2 seconds)
    int Timeout = 20;
    while (mRunning.load() && Timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        Timeout--;
    }

    mTaskHandle = nullptr;
    ESP_LOGI(TAG, "Sensor %d stopped", mConfig.SensorId);
    return ESP_OK;
}

void ObservableSensor::SetConfig(const SensorConfig& Config) {
    if (xSemaphoreTake(mConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint8_t Id = mConfig.SensorId;  // Preserve sensor ID
        mConfig = Config;
        mConfig.SensorId = Id;
        xSemaphoreGive(mConfigMutex);
    }
}

SensorConfig ObservableSensor::GetConfig() const {
    SensorConfig Result;
    if (xSemaphoreTake(mConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Result = mConfig;
        xSemaphoreGive(mConfigMutex);
    }
    return Result;
}

/*******************************************************************************
 * Synchronous Operations
 ******************************************************************************/

esp_err_t ObservableSensor::ReadSync(SensorData& Data) {
    return ReadHardware(Data);
}

SensorData ObservableSensor::GetLastReading() const {
    SensorData Result;
    if (xSemaphoreTake(mDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Result = mLastData;
        xSemaphoreGive(mDataMutex);
    }
    return Result;
}

/*******************************************************************************
 * Observable Subscriptions
 ******************************************************************************/

SubscriptionId ObservableSensor::OnData(Observer<SensorData> ObserverCallback) {
    return mDataObservable.Subscribe(std::move(ObserverCallback));
}

SubscriptionId ObservableSensor::OnError(Observer<SensorError> ObserverCallback) {
    return mErrorObservable.Subscribe(std::move(ObserverCallback));
}

SubscriptionId ObservableSensor::OnThreshold(Observer<ThresholdEvent> ObserverCallback) {
    return mThresholdObservable.Subscribe(std::move(ObserverCallback));
}

SubscriptionId ObservableSensor::OnLifecycle(Observer<LifecycleEvent> ObserverCallback) {
    return mLifecycleObservable.Subscribe(std::move(ObserverCallback));
}

SubscriptionId ObservableSensor::OnAny(Observer<const IModel*> ObserverCallback) {
    return mAnyObservable.Subscribe(std::move(ObserverCallback));
}

bool ObservableSensor::UnsubscribeData(SubscriptionId Id) {
    return mDataObservable.Unsubscribe(Id);
}

bool ObservableSensor::UnsubscribeError(SubscriptionId Id) {
    return mErrorObservable.Unsubscribe(Id);
}

bool ObservableSensor::UnsubscribeThreshold(SubscriptionId Id) {
    return mThresholdObservable.Unsubscribe(Id);
}

bool ObservableSensor::UnsubscribeLifecycle(SubscriptionId Id) {
    return mLifecycleObservable.Unsubscribe(Id);
}

bool ObservableSensor::UnsubscribeAny(SubscriptionId Id) {
    return mAnyObservable.Unsubscribe(Id);
}

Subscription<SensorData> ObservableSensor::SubscribeData(Observer<SensorData> ObserverCallback) {
    auto Id = mDataObservable.Subscribe(std::move(ObserverCallback));
    return Subscription<SensorData>(mDataObservable, Id);
}

Subscription<SensorError> ObservableSensor::SubscribeError(Observer<SensorError> ObserverCallback) {
    auto Id = mErrorObservable.Subscribe(std::move(ObserverCallback));
    return Subscription<SensorError>(mErrorObservable, Id);
}

Subscription<ThresholdEvent> ObservableSensor::SubscribeThreshold(Observer<ThresholdEvent> ObserverCallback) {
    auto Id = mThresholdObservable.Subscribe(std::move(ObserverCallback));
    return Subscription<ThresholdEvent>(mThresholdObservable, Id);
}

Subscription<LifecycleEvent> ObservableSensor::SubscribeLifecycle(Observer<LifecycleEvent> ObserverCallback) {
    auto Id = mLifecycleObservable.Subscribe(std::move(ObserverCallback));
    return Subscription<LifecycleEvent>(mLifecycleObservable, Id);
}

Subscription<const IModel*> ObservableSensor::SubscribeAny(Observer<const IModel*> ObserverCallback) {
    auto Id = mAnyObservable.Subscribe(std::move(ObserverCallback));
    return Subscription<const IModel*>(mAnyObservable, Id);
}

/*******************************************************************************
 * Polymorphic Event Notification
 ******************************************************************************/

void ObservableSensor::NotifyAny(const IModel* Model) {
    // Skip notification if no subscribers (optimization)
    if (mAnyObservable.HasSubscribers()) {
        mAnyObservable.Notify(Model);
    }
}

/*******************************************************************************
 * Hardware Interface (Override for real sensors)
 ******************************************************************************/

esp_err_t ObservableSensor::ReadHardware(SensorData& Data) {
    // Simulated sensor reading - REPLACE WITH ACTUAL HARDWARE CODE
    // Example: ADC read, I2C/SPI sensor communication, etc.

    static int32_t SimulatedValue = 50;

    // Simulate changing values (random walk)
    SimulatedValue += static_cast<int32_t>(esp_random() % 11) - 5;
    if (SimulatedValue < 0) SimulatedValue = 0;
    if (SimulatedValue > 100) SimulatedValue = 100;

    Data.Value = SimulatedValue;
    Data.RawValue = SimulatedValue * 40;  // Simulate 12-bit ADC
    Data.Temperature = 25.0f + (SimulatedValue - 50) * 0.1f;
    Data.Humidity = 50.0f + (SimulatedValue - 50) * 0.5f;
    Data.TimestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    Data.SensorId = mConfig.SensorId;
    Data.Quality = 95;

    return ESP_OK;
}

/*******************************************************************************
 * Task Implementation
 ******************************************************************************/

void ObservableSensor::TaskEntry(void* Arg) {
    auto* Self = static_cast<ObservableSensor*>(Arg);
    Self->TaskLoop();
    vTaskDelete(nullptr);
}

void ObservableSensor::TaskLoop() {
    ESP_LOGI(TAG, "Sensor %d task started (interval=%ums)",
             mConfig.SensorId, mConfig.ReadIntervalMs);

    EmitLifecycle(LifecycleEvent::State::Started);

    while (!mShouldStop.load()) {
        SensorData Data;
        esp_err_t Err = ReadHardware(Data);

        if (Err == ESP_OK) {
            // Cache last reading
            if (xSemaphoreTake(mDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                mLastData = Data;
                xSemaphoreGive(mDataMutex);
            }

            // Notify observers (type-safe)
            mDataObservable.Notify(Data);
            // Notify polymorphic observers
            NotifyAny(&Data);

            // Check thresholds
            CheckThresholds(Data);

            ESP_LOGD(TAG, "Sensor %d: value=%d, temp=%.1f, humid=%.1f",
                     mConfig.SensorId, Data.Value, Data.Temperature, Data.Humidity);
        } else {
            // Notify error observers (type-safe)
            SensorError Error(Err, "Sensor read failed", mConfig.SensorId);
            Error.TimestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            mErrorObservable.Notify(Error);
            // Notify polymorphic observers
            NotifyAny(&Error);

            ESP_LOGE(TAG, "Sensor %d read error: %d", mConfig.SensorId, Err);
        }

        // Wait for next interval
        uint32_t Interval;
        if (xSemaphoreTake(mConfigMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            Interval = mConfig.ReadIntervalMs;
            xSemaphoreGive(mConfigMutex);
        } else {
            Interval = 1000;  // Fallback
        }

        vTaskDelay(pdMS_TO_TICKS(Interval));
    }

    EmitLifecycle(LifecycleEvent::State::Stopped);
    mRunning.store(false);

    ESP_LOGI(TAG, "Sensor %d task stopped", mConfig.SensorId);
}

void ObservableSensor::CheckThresholds(const SensorData& Data) {
    if (!mConfig.EnableThresholdEvents) {
        return;
    }

    int32_t NewState = 0;

    if (mConfig.ThresholdHigh != 0 && Data.Value > mConfig.ThresholdHigh) {
        NewState = 1;
    } else if (mConfig.ThresholdLow != 0 && Data.Value < mConfig.ThresholdLow) {
        NewState = -1;
    }

    // Only emit on state change
    if (NewState != mLastThresholdState) {
        if (NewState == 1) {
            ThresholdEvent Event(ThresholdEvent::Type::High,
                                Data.Value, mConfig.ThresholdHigh, mConfig.SensorId);
            Event.TimestampMs = Data.TimestampMs;
            mThresholdObservable.Notify(Event);
            NotifyAny(&Event);
            ESP_LOGW(TAG, "Sensor %d: HIGH threshold crossed (value=%d, threshold=%d)",
                     mConfig.SensorId, Data.Value, mConfig.ThresholdHigh);
        } else if (NewState == -1) {
            ThresholdEvent Event(ThresholdEvent::Type::Low,
                                Data.Value, mConfig.ThresholdLow, mConfig.SensorId);
            Event.TimestampMs = Data.TimestampMs;
            mThresholdObservable.Notify(Event);
            NotifyAny(&Event);
            ESP_LOGW(TAG, "Sensor %d: LOW threshold crossed (value=%d, threshold=%d)",
                     mConfig.SensorId, Data.Value, mConfig.ThresholdLow);
        }

        mLastThresholdState = NewState;
    }
}

void ObservableSensor::EmitLifecycle(LifecycleEvent::State State) {
    LifecycleEvent Event(State, mConfig.SensorId);
    Event.TimestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    mLifecycleObservable.Notify(Event);
    NotifyAny(&Event);
}

} // namespace Sensor
} // namespace Arcana
