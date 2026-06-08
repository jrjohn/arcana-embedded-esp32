#pragma once
#include "FreeRTOS.h"

#define tskIDLE_PRIORITY ((UBaseType_t)0)
#define tskNO_AFFINITY   ((BaseType_t)0x7FFFFFFF)

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char* pcName, uint32_t usStackDepth,
                       void* pvParameters, UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode, const char* pcName,
                                   uint32_t usStackDepth, void* pvParameters,
                                   UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask,
                                   BaseType_t xCoreID);
void       vTaskDelete(TaskHandle_t xTaskToDelete);
void       vTaskDelay(TickType_t xTicksToDelay);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify);
uint32_t   ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
#ifdef __cplusplus
}
#endif
