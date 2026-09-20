#pragma once
#include <stddef.h>
#include "gpio.h"

#define I2S_TONE_SAMPLE_RATE    48000
#define I2S_TONE_BIT_DEPTH      32

esp_err_t i2s_init();
esp_err_t i2s_play_tone(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t i2s_enable_channel();
esp_err_t i2s_disable_channel();

esp_err_t i2s_play_beep(uint32_t freq_hz, uint32_t duration_ms);

// Retunes the channel to match a PCM stream so samples can be sent through
// without resampling. The channel must be disabled first, and every caller has
// to set the format it expects rather than assuming the one left behind by the
// previous caller. bit_depth takes 16 or 32; num_channels takes 1 or 2.
esp_err_t i2s_configure_stream(uint32_t sample_rate, uint16_t bit_depth, uint16_t num_channels);

// Writes PCM samples to the enabled channel, blocking until the DMA buffers
// have room. len must be a whole number of frames.
esp_err_t i2s_write(const void* data, size_t len, size_t* bytes_written);