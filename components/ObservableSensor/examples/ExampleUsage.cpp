/*
 * Observable Sensor - C++ Example Usage
 *
 * Demonstrates RTOS + Observable pattern with modern C++
 * Copy to main/Main.cpp to test
 */

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "ObservableSensor.hpp"

// Note: Include "nvs_flash.h" and call nvs_flash_init() in your main app

using namespace Arcana;
using namespace Arcana::Sensor;

static const char* TAG = "SensorExample";

/*******************************************************************************
 * Example 1: Basic Usage with Lambda
 ******************************************************************************/

void ExampleBasicUsage() {
    ESP_LOGI(TAG, "=== Example 1: Basic Usage ===");

    // Create sensor with fluent configuration
    auto Config = SensorConfig()
        .WithId(1)
        .WithInterval(2000);  // 2 seconds

    ObservableSensor Sensor(Config);

    // Subscribe with lambda (modern C++ style)
    Sensor.OnData([](const SensorData& Data) {
        ESP_LOGI(TAG, "[Lambda] Sensor %d: Value=%d, Temp=%.1fC",
                 Data.SensorId, Data.Value, Data.Temperature);
    });

    Sensor.Start();

    // Run for 10 seconds
    vTaskDelay(pdMS_TO_TICKS(10000));

    Sensor.Stop();
}

/*******************************************************************************
 * Example 2: Multiple Observers with Different Events
 ******************************************************************************/

void ExampleMultipleObservers() {
    ESP_LOGI(TAG, "=== Example 2: Multiple Observers ===");

    auto Config = SensorConfig()
        .WithId(2)
        .WithInterval(1000)
        .WithThresholds(20, 80);  // Low=20, High=80

    ObservableSensor Sensor(Config);

    // Data observer
    Sensor.OnData([](const SensorData& Data) {
        ESP_LOGI(TAG, "[Data] Value=%d, Quality=%d%%",
                 Data.Value, Data.Quality);
    });

    // Threshold observer
    Sensor.OnThreshold([](const ThresholdEvent& Event) {
        const char* Type = (Event.EventType == ThresholdEvent::Type::High) ? "HIGH" : "LOW";
        ESP_LOGW(TAG, "[Threshold] %s alert! Value=%d, Threshold=%d",
                 Type, Event.Value, Event.Threshold);
    });

    // Error observer
    Sensor.OnError([](const SensorError& Error) {
        ESP_LOGE(TAG, "[Error] Code=%d, Msg=%s",
                 Error.ErrorCode, Error.Message.c_str());
    });

    // Lifecycle observer
    Sensor.OnLifecycle([](const LifecycleEvent& Event) {
        const char* States[] = {"Started", "Stopped", "Initialized", "Deinitialized"};
        ESP_LOGI(TAG, "[Lifecycle] Sensor %d: %s",
                 Event.SensorId, States[static_cast<int>(Event.CurrentState)]);
    });

    Sensor.Start();
    vTaskDelay(pdMS_TO_TICKS(15000));
    Sensor.Stop();
}

/*******************************************************************************
 * Example 3: RAII Subscription (Auto-cleanup)
 ******************************************************************************/

void ExampleRAIISubscription() {
    ESP_LOGI(TAG, "=== Example 3: RAII Subscription ===");

    auto Config = SensorConfig().WithId(3).WithInterval(500);
    ObservableSensor Sensor(Config);

    {
        // Subscription auto-unsubscribes when going out of scope
        auto Sub = Sensor.SubscribeData([](const SensorData& Data) {
            ESP_LOGI(TAG, "[RAII] Value=%d", Data.Value);
        });

        Sensor.Start();
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "Subscription active: %s", Sub.IsActive() ? "yes" : "no");

        // Sub goes out of scope here - auto unsubscribes
    }

    ESP_LOGI(TAG, "Subscription released, but sensor still running...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // No more callbacks

    Sensor.Stop();
}

/*******************************************************************************
 * Example 4: Multiple Sensors with Shared Observer
 ******************************************************************************/

void ExampleMultipleSensors() {
    ESP_LOGI(TAG, "=== Example 4: Multiple Sensors ===");

    // Shared observer that handles all sensors
    auto SharedHandler = [](const SensorData& Data) {
        ESP_LOGI(TAG, "[Shared] Sensor %d: Value=%d, Temp=%.1fC",
                 Data.SensorId, Data.Value, Data.Temperature);
    };

    // Create multiple sensors
    std::vector<SensorPtr> Sensors;

    for (int i = 0; i < 3; i++) {
        auto Config = SensorConfig()
            .WithId(10 + i)
            .WithInterval(1000 + i * 300);

        auto Sensor = CreateSensor(Config);
        Sensor->OnData(SharedHandler);
        Sensor->Start();
        Sensors.push_back(std::move(Sensor));
    }

    vTaskDelay(pdMS_TO_TICKS(10000));

    // All sensors auto-cleanup when vector is destroyed
    Sensors.clear();
    ESP_LOGI(TAG, "All sensors cleaned up");
}

/*******************************************************************************
 * Example 5: Synchronous Reading
 ******************************************************************************/

void ExampleSyncRead() {
    ESP_LOGI(TAG, "=== Example 5: Synchronous Reading ===");

    auto Sensor = CreateSensor(SensorConfig().WithId(99));

    // Read without starting the async task
    for (int i = 0; i < 5; i++) {
        SensorData Data;
        if (Sensor->ReadSync(Data) == ESP_OK) {
            ESP_LOGI(TAG, "[Sync] Read %d: Value=%d, Temp=%.1f",
                     i, Data.Value, Data.Temperature);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*******************************************************************************
 * Example 6: Custom Sensor (Inheritance)
 ******************************************************************************/

class TemperatureSensor : public ObservableSensor {
public:
    TemperatureSensor(uint8_t Id)
        : ObservableSensor(SensorConfig().WithId(Id).WithInterval(1000)) {}

protected:
    esp_err_t ReadHardware(SensorData& Data) override {
        // Custom hardware reading logic
        // Example: Read from I2C temperature sensor
        static float Temp = 25.0f;
        Temp += (static_cast<float>(esp_random() % 100) - 50) / 100.0f;

        Data.Temperature = Temp;
        Data.Value = static_cast<int32_t>(Temp * 100);
        Data.SensorId = GetConfig().SensorId;
        Data.Quality = 98;
        Data.TimestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        return ESP_OK;
    }
};

void ExampleCustomSensor() {
    ESP_LOGI(TAG, "=== Example 6: Custom Sensor ===");

    TemperatureSensor TempSensor(50);

    TempSensor.OnData([](const SensorData& Data) {
        ESP_LOGI(TAG, "[TempSensor] Temperature: %.2fC", Data.Temperature);
    });

    TempSensor.Start();
    vTaskDelay(pdMS_TO_TICKS(5000));
    TempSensor.Stop();
}

/*******************************************************************************
 * Example 7: Chained Observers (Processing Pipeline)
 ******************************************************************************/

void ExampleProcessingPipeline() {
    ESP_LOGI(TAG, "=== Example 7: Processing Pipeline ===");

    auto Sensor = CreateSensor(SensorConfig().WithId(7).WithInterval(500));

    // Stage 1: Raw data logging
    Sensor->OnData([](const SensorData& Data) {
        ESP_LOGD(TAG, "[Stage1] Raw: %d", Data.Value);
    });

    // Stage 2: Filtering (only report significant changes)
    static int32_t LastReported = 0;
    Sensor->OnData([](const SensorData& Data) {
        if (std::abs(Data.Value - LastReported) > 5) {
            ESP_LOGI(TAG, "[Stage2] Significant change: %d -> %d",
                     LastReported, Data.Value);
            LastReported = Data.Value;
        }
    });

    // Stage 3: Statistics
    static int32_t Sum = 0;
    static int Count = 0;
    Sensor->OnData([](const SensorData& Data) {
        Sum += Data.Value;
        Count++;
        if (Count % 10 == 0) {
            ESP_LOGI(TAG, "[Stage3] Average (last 10): %.1f",
                     static_cast<float>(Sum) / 10.0f);
            Sum = 0;
        }
    });

    Sensor->Start();
    vTaskDelay(pdMS_TO_TICKS(10000));
    Sensor->Stop();
}

/*******************************************************************************
 * Main Application
 ******************************************************************************/

extern "C" void app_main() {
    ESP_LOGI(TAG, "[APP] Observable Sensor C++ Examples");
    ESP_LOGI(TAG, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());

    // Note: Call nvs_flash_init() here if needed for your application

    // Run examples
    ExampleBasicUsage();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ExampleMultipleObservers();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ExampleRAIISubscription();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ExampleMultipleSensors();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ExampleSyncRead();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ExampleCustomSensor();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ExampleProcessingPipeline();

    ESP_LOGI(TAG, "[APP] All examples completed!");
}
