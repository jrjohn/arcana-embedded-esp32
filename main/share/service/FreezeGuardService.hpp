#pragma once

#include "FreezeTypes.hpp"
#include "esp_err.h"
#include <cstdint>

namespace Arcana::Freeze {

/**
 * Task-watchdog guard for long-running loops (AGENTS.md §6, rule R11).
 *
 * sdkconfig.defaults sets CONFIG_ESP_TASK_WDT_PANIC=y with a 10 s timeout: a subscribed
 * loop that does not feed within 10 s panics and reboots instead of hanging silently.
 * The last fed stage survives the reboot and is printed once on the next boot
 * ("LC_FREEZE reset=task_wdt stage=<n> uptime=<s>").
 *
 * Usage, in the loop's own task:
 *     guard->watchCurrentTask();
 *     for (;;) { guard->feed(stage); ...work bounded well under 10 s... }
 * For a known-slow, bounded call (Wi-Fi connect, SNTP wait, an upload that ends in a
 * reboot) wrap it in pause()/resume() — never esp_task_wdt_delete() the loop for good.
 */
class FreezeGuardService {
public:
    virtual ~FreezeGuardService() = default;

    virtual esp_err_t init_HAL() = 0;   // reads the reset reason, prints LC_FREEZE once
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    /// Subscribe the calling task to the task watchdog.
    virtual esp_err_t watchCurrentTask() = 0;
    /// Record the stage and reset the watchdog for the calling task.
    virtual void feed(Stage stage) = 0;
    /// Unsubscribe the calling task around a known-slow call; `reason` is logged.
    virtual void pause(const char* reason) = 0;
    /// Re-subscribe after pause().
    virtual void resume() = 0;
    virtual int watchdogTimeoutS() const = 0;
};

} // namespace Arcana::Freeze
