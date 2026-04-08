#pragma once

#include "Observable.hpp"
#include "SensorTypes.hpp"
#include "StorageTypes.hpp"
#include "TimerTypes.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <atomic>

namespace Arcana::Lcd {

/// Dirty flags for diff-based rendering
enum DirtyFlag : uint8_t {
    DIRTY_SENSOR  = 0x01,
    DIRTY_STORAGE = 0x02,
    DIRTY_TIME    = 0x04,
    DIRTY_TOAST   = 0x08,
    DIRTY_ALL     = 0xFF,
};

/// ViewModel output — pure data, no rendering
struct LcdOutput {
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint32_t records = 0;
    uint16_t rate = 0;        ///< records/sec
    uint32_t uptimeSec = 0;

    // Toast overlay
    char toastMsg[22] = {};   ///< Toast message (max 21 chars + null)
    uint32_t toastExpiry = 0; ///< Tick when toast expires (0 = no toast)

    uint8_t dirty = DIRTY_ALL;
};

/**
 * ViewModel: subscribes to Service Observables, transforms data into LcdOutput.
 * Pure state transformation — no rendering code.
 */
class LcdViewModel {
public:
    struct Input {
        Observable<Sensor::SensorData>* SensorData = nullptr;
        Observable<Storage::StorageStats>* StorageStats = nullptr;
        Observable<Timer::TimerTick>* BaseTimer = nullptr;
    };

    Input input;

    /// Initialize subscriptions. Call after wiring inputs.
    /// renderTask: task to notify when output changes.
    void init(TaskHandle_t renderTask = nullptr) {
        mRenderTask = renderTask;

        if (input.SensorData) {
            input.SensorData->Subscribe([this](const Sensor::SensorData& d) {
                mOutput.temperature = d.Temperature;
                mOutput.humidity = d.Humidity;
                mOutput.dirty |= DIRTY_SENSOR;
                notifyRender();
            });
        }

        if (input.StorageStats) {
            input.StorageStats->Subscribe([this](const Storage::StorageStats& s) {
                mOutput.records = s.recordCount;
                mOutput.rate = s.writesPerSec;
                mOutput.dirty |= DIRTY_STORAGE;
                notifyRender();
            });
        }

        if (input.BaseTimer) {
            input.BaseTimer->Subscribe([this](const Timer::TimerTick& t) {
                mOutput.uptimeSec = (uint32_t)(t.Timestamp / 1000000ULL);
                mOutput.dirty |= DIRTY_TIME;
                notifyRender();
            });
        }
    }

    /// Access current output (read by View)
    LcdOutput& output() { return mOutput; }

    /// Show a toast message for durationMs (0 = indefinite)
    void showToast(const char* msg, uint32_t durationMs = 5000) {
        strncpy(mOutput.toastMsg, msg, sizeof(mOutput.toastMsg) - 1);
        mOutput.toastMsg[sizeof(mOutput.toastMsg) - 1] = '\0';
        mOutput.toastExpiry = (durationMs == 0)
            ? 0xFFFFFFFF  // indefinite
            : xTaskGetTickCount() + pdMS_TO_TICKS(durationMs);
        mOutput.dirty |= DIRTY_TOAST;
        notifyRender();
    }

    /// Dismiss current toast
    void dismissToast() {
        mOutput.toastMsg[0] = '\0';
        mOutput.toastExpiry = 0;
        mOutput.dirty |= DIRTY_TOAST | DIRTY_ALL;  // redraw everything
        notifyRender();
    }

private:
    void notifyRender() {
        if (mRenderTask) {
            xTaskNotifyGive(mRenderTask);
        }
    }

    LcdOutput mOutput;
    TaskHandle_t mRenderTask = nullptr;
};

} // namespace Arcana::Lcd
