#pragma once
#include "gpio.h"

esp_err_t spi_init();
esp_err_t spi_write_cmd(uint8_t cmd);
esp_err_t spi_write_data(const uint8_t* data, size_t len);