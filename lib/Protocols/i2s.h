#pragma once
#include "gpio.h"

#define I2S_BCLK_PIN    GPIO_NUM_4
#define I2S_WS_PIN      GPIO_NUM_6
#define I2S_DIN_PIN     GPIO_NUM_7

esp_err_t i2s_init();
esp_err_t i2s_play_tone(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t i2s_enable_channel();
esp_err_t i2s_disable_channel();

esp_err_t i2s_play_beep(uint32_t freq_hz, uint32_t duration_ms);