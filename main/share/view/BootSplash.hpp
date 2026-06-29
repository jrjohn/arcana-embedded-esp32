/**
 * @file BootSplash.hpp
 * @brief Animated "initializing" screen shown DURING the SD-card init.
 *
 * On a cold boot the sdspi/SdFat card init can stall for tens of seconds (busy-
 * card retry loop) inside AppContainer::initHAL() — on the main boot thread,
 * before the MainView render task exists. A black panel that whole time looks
 * like a hang. This spawns a tiny low-priority task (right after the LCD is up,
 * before the SD init) that animates a spinner + elapsed-seconds counter so the
 * user can see the device is alive and working, then hands the panel back to
 * MainView via stop().
 *
 * LCD and SD share SPI2; both drivers are polling (separate CS, bus-lock
 * arbitrated) — the same supported arrangement under which the view refresh and
 * the ATS writer already run together — so drawing during SD init is safe. The
 * task draws infrequently (~300 ms) to keep bus contention negligible.
 */
#pragma once

#include "Ssd1306.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

namespace Arcana::View {

class BootSplash {
public:
    /** Start the animation on `disp`. Call after LcdService::init_HAL(), before
     *  the (slow) storage init. No-op-safe if the panel is absent (Ssd1306 gates
     *  its own traffic when not ready). */
    static void start(Lcd::Ssd1306* disp) {
        sDisp = disp;
        sStop = false;
        sDone = false;
        if (!disp) { sDone = true; return; }
        xTaskCreatePinnedToCore(taskFn, "bootsplash", 3072, nullptr,
                                tskIDLE_PRIORITY + 1, &sTask, 1 /* APP core */);
    }

    /** Signal the splash to finish and wait (≤2 s) for it to release the panel,
     *  so MainView can take over without racing the shared framebuffer. */
    static void stop() {
        sStop = true;
        for (int i = 0; i < 200 && !sDone; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

private:
    static void taskFn(void*) {
        static const char kSpin[4] = {'|', '/', '-', '\\'};
        const uint32_t t0 = xTaskGetTickCount();
        uint8_t frame = 0;
        while (!sStop) {
            uint32_t secs = (xTaskGetTickCount() - t0) / configTICK_RATE_HZ;
            char line[22];
            sDisp->Clear();
            sDisp->DrawStringAt(7, 1, "ARCANA");
            sDisp->DrawStringAt(2, 3, "Initializing");
            snprintf(line, sizeof(line), "SD card %c", kSpin[frame & 3]);
            sDisp->DrawStringAt(2, 4, line);
            snprintf(line, sizeof(line), "elapsed %lus", (unsigned long)secs);
            sDisp->DrawStringAt(2, 6, line);
            sDisp->Display();
            frame++;
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        sDisp->Clear();
        sDisp->Display();
        sDone = true;
        vTaskDelete(nullptr);
    }

    static inline Lcd::Ssd1306*  sDisp = nullptr;
    static inline volatile bool  sStop = false;
    static inline volatile bool  sDone = false;
    static inline TaskHandle_t   sTask = nullptr;
};

} // namespace Arcana::View
