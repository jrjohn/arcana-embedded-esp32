/*
 * Observable Sensor Component for ESP32
 *
 * C++ implementation of RTOS + Observable pattern
 * Thread-safe sensor reading with event-driven architecture
 */

#pragma once

#include "Observable.hpp"
#include "SensorTypes.hpp"

#include <memory>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"

namespace Arcana {
namespace Sensor {

/**
 * @brief Observable Sensor with RTOS task integration
 *
 * Provides:
 * - Periodic sensor reading in dedicated FreeRTOS task
 * - Thread-safe Observable pattern for event notification
 * - Threshold monitoring with automatic events
 * - Lifecycle management (start/stop/configure)
 *
 * Usage:
 * @code
 *   ObservableSensor Sensor(SensorConfig().WithId(1).WithInterval(1000));
 *
 *   Sensor.OnData([](const SensorData& Data) {
 *       printf("Value: %d\n", Data.Value);
 *   });
 *
 *   Sensor.Start();
 * @endcode
 */
class ObservableSensor {
public:
    /**
     * @brief Construct with configuration
     */
    explicit ObservableSensor(const SensorConfig& Config = SensorConfig());

    /**
     * @brief Destructor - stops task and cleans up
     */
    virtual ~ObservableSensor();

    // Non-copyable
    ObservableSensor(const ObservableSensor&) = delete;
    ObservableSensor& operator=(const ObservableSensor&) = delete;

    // Movable
    ObservableSensor(ObservableSensor&& Other) noexcept;
    ObservableSensor& operator=(ObservableSensor&& Other) noexcept;

    /***************************************************************************
     * Lifecycle Management
     **************************************************************************/

    /**
     * @brief Start sensor reading task
     * @return ESP_OK on success
     */
    esp_err_t Start();

    /**
     * @brief Stop sensor reading task
     * @return ESP_OK on success
     */
    esp_err_t Stop();

    /**
     * @brief Check if sensor is running
     */
    bool IsRunning() const { return mRunning.load(); }

    /**
     * @brief Update configuration at runtime
     */
    void SetConfig(const SensorConfig& Config);

    /**
     * @brief Get current configuration
     */
    SensorConfig GetConfig() const;

    /***************************************************************************
     * Synchronous Operations
     **************************************************************************/

    /**
     * @brief Read sensor synchronously (blocking)
     * @param[out] Data Output sensor data
     * @return ESP_OK on success
     */
    esp_err_t ReadSync(SensorData& Data);

    /**
     * @brief Get last reading (non-blocking)
     */
    SensorData GetLastReading() const;

    /***************************************************************************
     * Observable Events (Subscribe to these)
     **************************************************************************/

    /**
     * @brief Subscribe to sensor data events
     *
     * @param ObserverCallback Callback receiving SensorData
     * @return Subscription ID for unsubscribing
     */
    SubscriptionId OnData(Observer<SensorData> ObserverCallback);

    /**
     * @brief Subscribe to error events
     */
    SubscriptionId OnError(Observer<SensorError> ObserverCallback);

    /**
     * @brief Subscribe to threshold events
     */
    SubscriptionId OnThreshold(Observer<ThresholdEvent> ObserverCallback);

    /**
     * @brief Subscribe to lifecycle events
     */
    SubscriptionId OnLifecycle(Observer<LifecycleEvent> ObserverCallback);

    /**
     * @brief Subscribe to ALL events (polymorphic)
     *
     * Receives all event types through IModel interface.
     * Use GetType() or ModelCast<T>() to determine specific type.
     *
     * Usage:
     * @code
     *   Sensor.OnAny([](const IModel* Model) {
     *       ESP_LOGI(TAG, "Event: %s", Model->GetTypeName());
     *
     *       if (auto* Data = ModelCast<SensorData>(*Model)) {
     *           // Handle SensorData
     *       }
     *   });
     * @endcode
     */
    SubscriptionId OnAny(Observer<const IModel*> ObserverCallback);

    /**
     * @brief Unsubscribe from data events
     */
    bool UnsubscribeData(SubscriptionId Id);

    /**
     * @brief Unsubscribe from error events
     */
    bool UnsubscribeError(SubscriptionId Id);

    /**
     * @brief Unsubscribe from threshold events
     */
    bool UnsubscribeThreshold(SubscriptionId Id);

    /**
     * @brief Unsubscribe from lifecycle events
     */
    bool UnsubscribeLifecycle(SubscriptionId Id);

    /**
     * @brief Unsubscribe from any events
     */
    bool UnsubscribeAny(SubscriptionId Id);

    /***************************************************************************
     * RAII Subscription Helpers
     **************************************************************************/

    /**
     * @brief Subscribe with automatic cleanup
     *
     * Usage:
     * @code
     *   auto Sub = Sensor.SubscribeData([](const SensorData& D) { ... });
     *   // Automatically unsubscribes when 'Sub' goes out of scope
     * @endcode
     */
    Subscription<SensorData> SubscribeData(Observer<SensorData> ObserverCallback);
    Subscription<SensorError> SubscribeError(Observer<SensorError> ObserverCallback);
    Subscription<ThresholdEvent> SubscribeThreshold(Observer<ThresholdEvent> ObserverCallback);
    Subscription<LifecycleEvent> SubscribeLifecycle(Observer<LifecycleEvent> ObserverCallback);
    Subscription<const IModel*> SubscribeAny(Observer<const IModel*> ObserverCallback);

protected:
    /**
     * @brief Hardware read function - override for actual sensor
     *
     * Default implementation returns simulated data.
     * Override this method for real hardware.
     */
    virtual esp_err_t ReadHardware(SensorData& Data);

private:
    /**
     * @brief FreeRTOS task entry point
     */
    static void TaskEntry(void* Arg);

    /**
     * @brief Main task loop
     */
    void TaskLoop();

    /**
     * @brief Check thresholds and emit events if crossed
     */
    void CheckThresholds(const SensorData& Data);

    /**
     * @brief Emit lifecycle event
     */
    void EmitLifecycle(LifecycleEvent::State State);

    // Configuration
    SensorConfig mConfig;
    mutable SemaphoreHandle_t mConfigMutex;

    // Task management
    TaskHandle_t mTaskHandle;
    std::atomic<bool> mRunning;
    std::atomic<bool> mShouldStop;

    // Last reading cache
    SensorData mLastData;
    mutable SemaphoreHandle_t mDataMutex;

    // Threshold state (-1: below low, 0: normal, 1: above high)
    int32_t mLastThresholdState;

    // Observables (event sources)
    Observable<SensorData> mDataObservable;
    Observable<SensorError> mErrorObservable;
    Observable<ThresholdEvent> mThresholdObservable;
    Observable<LifecycleEvent> mLifecycleObservable;
    Observable<const IModel*> mAnyObservable;  ///< Polymorphic observable for all events

    /**
     * @brief Notify the polymorphic observable
     */
    void NotifyAny(const IModel* Model);
};

/**
 * @brief Smart pointer type for sensor management
 */
using SensorPtr = std::unique_ptr<ObservableSensor>;

/**
 * @brief Factory function for creating sensors
 */
inline SensorPtr CreateSensor(const SensorConfig& Config = SensorConfig()) {
    return std::make_unique<ObservableSensor>(Config);
}

} // namespace Sensor
} // namespace Arcana
