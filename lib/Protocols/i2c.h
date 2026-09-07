#pragma once
#include "gpio.h"
#include "driver/i2c_master.h"

#define I2C_DISPLAY_SDA_PIN GPIO_NUM_17
#define I2C_DISPLAY_SCL_PIN GPIO_NUM_18

esp_err_t i2c_init();
esp_err_t i2c_add_device_to_bus(i2c_master_dev_handle_t* dev_handle, uint16_t device_addr);
void i2c_probe_addresses();