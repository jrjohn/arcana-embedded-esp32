#pragma once
#include "FreeRTOS.h"

typedef void* SemaphoreHandle_t;
typedef struct StaticSemaphore_s { void* placeholder; } StaticSemaphore_t;

#ifdef __cplusplus
extern "C" {
#endif
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* pxMutexBuffer);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void              vSemaphoreDelete(SemaphoreHandle_t xSemaphore);
#ifdef __cplusplus
}
#endif
