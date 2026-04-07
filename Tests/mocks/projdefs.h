#pragma once
#include "portmacro.h"

#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
