#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Test injection: g_bt_set_name_result is checked by the inline stub.
// Tests can override it by including this header and assigning directly.
extern esp_err_t g_bt_set_name_result;

inline esp_err_t esp_ble_gap_set_device_name(const char* /*name*/) {
    return g_bt_set_name_result;
}

#ifdef __cplusplus
}
#endif
