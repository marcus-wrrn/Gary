#pragma once
#include <stddef.h>
#include "gpio.h"

#define I2S_TONE_SAMPLE_RATE    48000
#define I2S_TONE_BIT_DEPTH      32
#define I2S_READ_TIMEOUT_MS     1000

esp_err_t i2s_init();
esp_err_t i2s_play_tone(uint32_t freq_hz, uint32_t duration_ms);
// TX and RX are a full-duplex pair on one controller. The driver forces RX to
// slave and routes BCLK/WS to it from the TX module, so TX must be enabled for
// the mic to receive anything; enabling RX on its own fails. Enabling a channel
// that already runs is a no-op rather than an error.
esp_err_t i2s_enable_tx();
esp_err_t i2s_disable_tx();
esp_err_t i2s_enable_rx();
esp_err_t i2s_disable_rx();

esp_err_t i2s_play_beep(uint32_t freq_hz, uint32_t duration_ms);

// Retunes the bus to match a PCM stream so samples can be sent through without
// resampling. TX and RX are a full-duplex pair sharing one set of clock and
// slot registers, so this moves both and both must be disabled first. Every
// caller has to set the format it expects rather than assuming the one left
// behind by the previous caller. bit_depth takes 16 or 32; num_channels 1 or 2.
esp_err_t i2s_configure_stream(uint32_t sample_rate, uint16_t bit_depth, uint16_t num_channels);

// Writes PCM samples to the enabled channel, blocking until the DMA buffers
// have room. len must be a whole number of frames.
esp_err_t i2s_write(const void* data, size_t len, size_t* bytes_written);

// Reads PCM samples from the mic, blocking until the DMA buffers have filled or
// I2S_READ_TIMEOUT_MS passes. len must be a whole number of frames.
esp_err_t i2s_read(void* data, size_t len, size_t* bytes_read);