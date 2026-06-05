#pragma once
// Stub driver/i2c_master.h for unit tests.
// Function bodies are provided by the test that needs them (e.g.
// test_xl9555.cpp fakes a register file); link-time substitution.
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef void* i2c_master_bus_handle_t;
typedef void* i2c_master_dev_handle_t;

typedef int gpio_num_t;
#define GPIO_NUM_NC ((gpio_num_t)-1)
#define GPIO_NUM_41 ((gpio_num_t)41)
#define GPIO_NUM_42 ((gpio_num_t)42)

#define I2C_CLK_SRC_DEFAULT 0
#define I2C_NUM_0 0
#define I2C_NUM_1 1
#define I2C_ADDR_BIT_LEN_7 0

typedef struct {
    int clk_source;
    int i2c_port;
    gpio_num_t scl_io_num;
    gpio_num_t sda_io_num;
    int glitch_ignore_cnt;
    struct { unsigned enable_internal_pullup : 1; } flags;
} i2c_master_bus_config_t;

typedef struct {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* config,
                             i2c_master_bus_handle_t* ret_bus);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t* config,
                                    i2c_master_dev_handle_t* ret_dev);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t* data,
                              size_t len, int timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev,
                                      const uint8_t* tx, size_t tx_len,
                                      uint8_t* rx, size_t rx_len, int timeout_ms);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus);
#ifdef __cplusplus
}
#endif
