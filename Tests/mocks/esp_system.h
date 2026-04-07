#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
const char* esp_get_idf_version(void);
uint32_t    esp_get_free_heap_size(void);
uint32_t    esp_get_minimum_free_heap_size(void);
void        esp_restart(void);
#ifdef __cplusplus
}
#endif
