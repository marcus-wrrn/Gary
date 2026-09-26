#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "gpio.h"
#include "config.h"

esp_err_t i2s_init();
esp_err_t i2s_play_tone(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t i2s_enable_tx();
esp_err_t i2s_disable_tx();
esp_err_t i2s_enable_rx();
esp_err_t i2s_disable_rx();

bool i2s_has_aux();

esp_err_t i2s_start_capture();
esp_err_t i2s_stop_capture();

bool i2s_capture_overflowed();

esp_err_t i2s_play_beep(uint32_t freq_hz, uint32_t duration_ms);

esp_err_t i2s_configure_stream(uint32_t sample_rate, uint16_t bit_depth, uint16_t num_channels);

esp_err_t i2s_write(const void* data, size_t len, size_t* bytes_written);

esp_err_t i2s_read(void* data, size_t len, size_t* bytes_read);

esp_err_t i2s_read_aux(void* data, size_t len, size_t* bytes_read);