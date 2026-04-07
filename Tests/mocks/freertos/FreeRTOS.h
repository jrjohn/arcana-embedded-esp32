#pragma once
#include <stdint.h>
#include <stddef.h>
#include "portmacro.h"
#include "projdefs.h"

#define configASSERT(x) (void)(x)
#define portMAX_DELAY   ((TickType_t)0xFFFFFFFFUL)

#ifdef __cplusplus
extern "C" {
#endif
TickType_t xTaskGetTickCount(void);
#ifdef __cplusplus
}
#endif
