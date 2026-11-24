#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// Host stub: defined by the test that needs it (test_freeze_guard_service.cpp).
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_task_wdt_add(TaskHandle_t task);
esp_err_t esp_task_wdt_delete(TaskHandle_t task);
esp_err_t esp_task_wdt_reset(void);
#ifdef __cplusplus
}
#endif
