#pragma once
#include "gpio.h"

#define I2S_BCLK_PIN    GPIO_NUM_20
#define I2S_WS_PIN      GPIO_NUM_3
#define I2S_DIN_PIN     GPIO_NUM_15

esp_err_t i2s_init();