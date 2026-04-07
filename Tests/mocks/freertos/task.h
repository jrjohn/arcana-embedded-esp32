#pragma once
#include "FreeRTOS.h"

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char* pcName, uint32_t usStackDepth,
                       void* pvParameters, UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask);
void       vTaskDelete(TaskHandle_t xTaskToDelete);
void       vTaskDelay(TickType_t xTicksToDelay);
TickType_t xTaskGetTickCount(void);
#ifdef __cplusplus
}
#endif
