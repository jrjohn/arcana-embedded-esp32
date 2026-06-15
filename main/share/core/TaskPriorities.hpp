#pragma once

/*
 * Task priority + core-affinity map — single source of truth.
 *
 * Priorities (higher preempts lower). System reference points on ESP32:
 * WiFi task 23, BT controller 23+, esp_timer dispatch 22, lwIP tcpip 18 —
 * application tasks stay well below so the radio stacks are never starved.
 *
 * Core affinity: the radio stacks (WiFi/BT) and their ISRs run on PRO core
 * (0). Bulk I/O and rendering pin to APP core (1) so SD write bursts and
 * LCD flushes do not jitter network timing — and vice versa.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Arcana {
namespace TaskCfg {

// ── Priorities ──────────────────────────────────────────────────────────
constexpr UBaseType_t kPrioEventQueue = 5;                      // Observable async dispatch
constexpr UBaseType_t kPrioSensor     = 5;                      // sensor sampling task
constexpr UBaseType_t kPrioLedCycle   = 3;                      // cosmetic LED cycling
constexpr UBaseType_t kPrioRender     = tskIDLE_PRIORITY + 2;   // LCD view refresh
// ECG capture integrity: the SD writer must not be starved by render/app tasks
// on the APP core, or it falls behind during contention and records back up
// (with Block overflow → catch-up; previously Drop → lost samples). Raised from
// IDLE+1 to 5 — fair share with sensor/command (also 5), still far below the
// WiFi/BT radios (23) which live on the PRO core, so it can't jitter the stacks.
constexpr UBaseType_t kPrioStorage    = 5;                      // ECG/SD writer (data-integrity critical)
constexpr UBaseType_t kPrioIoPoll     = tskIDLE_PRIORITY + 1;   // button polling

// ── Core affinity ───────────────────────────────────────────────────────
constexpr BaseType_t kCoreNetwork = 0;              // PRO core: WiFi/BT/lwIP
constexpr BaseType_t kCoreApp     = 1;              // APP core: storage + render
constexpr BaseType_t kCoreAny     = tskNO_AFFINITY;

} // namespace TaskCfg
} // namespace Arcana
