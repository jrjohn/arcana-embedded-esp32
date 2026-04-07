#pragma once
#include <stdint.h>

/* Port-level types — must come before FreeRTOS.h redefines */
typedef uint32_t      StackType_t;
typedef long          BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t      TickType_t;
