#pragma once
#include "gpio.h"

esp_err_t i2s_init();
esp_err_t i2s_play_tone(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t i2s_enable_channel();
esp_err_t i2s_disable_channel();

esp_err_t i2s_play_beep(uint32_t freq_hz, uint32_t duration_ms);