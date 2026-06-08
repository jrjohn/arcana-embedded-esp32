#pragma once

#include "BaseLcdView.hpp"
#include "TaskPriorities.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

namespace Arcana::Lcd {

/**
 * MainView — concrete View for 128x64 SSD1306 OLED.
 * Renders: title, temp, humi, records, rate.
 * Diff-based rendering via dirty flags.
 *
 * Owns a FreeRTOS render task that blocks on xTaskNotifyWait.
 */
class MainView : public BaseLcdView {
public:
    struct Input {
        MainViewModel* viewModel = nullptr;
        Ssd1306* display = nullptr;
    };

    Input input;

    void onEnter(Ssd1306& display) override {
        display.Clear();
        display.DrawStringAt(0, 0, "  Arcana ESP32");
        display.DrawStringAt(0, 1, "--------------------");
        display.DrawStringAt(0, 2, "Temp:   --.- C");
        display.DrawStringAt(0, 3, "Humi:   --.- %");
        display.DrawStringAt(0, 4, "--------------------");
        display.DrawStringAt(0, 5, "Records: 0");
        display.DrawStringAt(0, 6, "Rate: 0 rec/s");
        display.Display();
    }

    void render(Ssd1306& display, LcdOutput& out) override {
        char line[22];

        // Toast active → show full-screen toast (like STM32)
        if (out.toastExpiry > 0) {
            // Check expiry
            if (out.toastExpiry != 0xFFFFFFFF &&
                xTaskGetTickCount() >= out.toastExpiry) {
                // Toast expired → clear and redraw normal view
                out.toastMsg[0] = '\0';
                out.toastExpiry = 0;
                out.dirty = DIRTY_ALL;
                onEnter(display);
                return;
            }

            if (out.dirty & DIRTY_TOAST) {
                display.Clear();
                display.DrawStringAt(0, 0, "====================");
                display.DrawStringAt(0, 3, out.toastMsg);
                display.DrawStringAt(0, 7, "====================");
                display.Display();
            }
            out.dirty = 0;
            return;
        }

        // Normal view
        if (out.dirty & DIRTY_SENSOR) {
            snprintf(line, sizeof(line), "Temp:  %5.1f C", out.temperature);
            display.DrawStringAt(0, 2, line);

            snprintf(line, sizeof(line), "Humi:  %5.1f %%", out.humidity);
            display.DrawStringAt(0, 3, line);
        }

        if (out.dirty & DIRTY_STORAGE) {
            snprintf(line, sizeof(line), "Records: %-9lu", (unsigned long)out.records);
            display.DrawStringAt(0, 5, line);

            snprintf(line, sizeof(line), "Rate: %-5u rec/s", out.rate);
            display.DrawStringAt(0, 6, line);
        }

        out.dirty = 0;
        display.Display();
    }

    void onExit(Ssd1306& display) override {
        display.Clear();
        display.Display();
    }

    /// Start the render task (APP core — keeps LCD SPI flushes off the
    /// radio core, see core/TaskPriorities.hpp)
    void start() {
        xTaskCreatePinnedToCore(renderTaskFunc, "LcdView", 2048, this,
                                TaskCfg::kPrioRender, &mTaskHandle, TaskCfg::kCoreApp);
    }

    TaskHandle_t taskHandle() const { return mTaskHandle; }

    /**
     * @brief Initialize wired display + perform one render iteration.
     *        Public for tests so the render-task body can be exercised
     *        without spawning a real FreeRTOS task. Returns false if
     *        viewModel/display weren't wired.
     */
    bool renderTaskStep() {
        auto* vm = input.viewModel;
        auto* disp = input.display;
        if (!vm || !disp) return false;
        if (vm->output().dirty) {
            render(*disp, vm->output());
        }
        return true;
    }

    /**
     * @brief Render task function — public so tests can invoke it via the
     *        same call shape as xTaskCreate. Production code uses it only
     *        as the function pointer passed to xTaskCreate.
     */
    static void renderTaskFunc(void* param) {
        auto* self = static_cast<MainView*>(param);
        auto* vm = self->input.viewModel;
        auto* disp = self->input.display;
        if (!vm || !disp) { vTaskDelete(nullptr); return; }

        self->onEnter(*disp);

        for (;;) {
            // Block until ViewModel notifies us (or timeout 500ms for safety)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

            if (vm->output().dirty) {
                self->render(*disp, vm->output());
            }
        }
    }

private:
    TaskHandle_t mTaskHandle = nullptr;
};

} // namespace Arcana::Lcd
