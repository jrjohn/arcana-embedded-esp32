#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
inline esp_err_t esp_ble_gap_set_device_name(const char*) { return ESP_OK; }
#ifdef __cplusplus
}
#endif
