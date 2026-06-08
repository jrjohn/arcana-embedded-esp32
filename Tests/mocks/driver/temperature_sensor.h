#pragma once
// Stub driver/temperature_sensor.h for unit tests.
// Function bodies live in the test (test_tsens_sensor.cpp).
#include "esp_err.h"

typedef void* temperature_sensor_handle_t;

typedef struct {
    int range_min;
    int range_max;
    int clk_src;
} temperature_sensor_config_t;

#define TEMPERATURE_SENSOR_CONFIG_DEFAULT(min, max) \
    { .range_min = (min), .range_max = (max), .clk_src = 0 }

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t temperature_sensor_install(const temperature_sensor_config_t* cfg,
                                     temperature_sensor_handle_t* ret_handle);
esp_err_t temperature_sensor_enable(temperature_sensor_handle_t handle);
esp_err_t temperature_sensor_disable(temperature_sensor_handle_t handle);
esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t handle);
esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t handle,
                                         float* out_celsius);
#ifdef __cplusplus
}
#endif
