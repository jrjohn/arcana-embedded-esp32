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
constexpr UBaseType_t kPrioStorage    = tskIDLE_PRIORITY + 1;   // bulk SD writer
constexpr UBaseType_t kPrioIoPoll     = tskIDLE_PRIORITY + 1;   // button polling

// ── Core affinity ───────────────────────────────────────────────────────
constexpr BaseType_t kCoreNetwork = 0;              // PRO core: WiFi/BT/lwIP
constexpr BaseType_t kCoreApp     = 1;              // APP core: storage + render
constexpr BaseType_t kCoreAny     = tskNO_AFFINITY;

} // namespace TaskCfg
} // namespace Arcana
