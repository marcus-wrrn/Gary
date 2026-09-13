#pragma once
#include "gpio.h"
#include "driver/i2c_master.h"

esp_err_t i2c_init();
esp_err_t i2c_add_device_to_bus(i2c_master_dev_handle_t* dev_handle, uint16_t device_addr);
void i2c_probe_addresses();