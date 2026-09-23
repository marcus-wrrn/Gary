#pragma once
#include <stdbool.h>
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

// The third mic sits on a second controller, a receive-only slave whose BCLK
// and WS pins are jumpered to the ones TX drives. False when the board has no
// I2S_DIN2_PIN, in which case capture carries on with the main pair alone.
bool i2s_has_aux();

// Starts the main and aux receivers so they land on the same frame: both are
// armed before TX starts the clock, which enabling RX on its own can't
// promise. Everything must be disabled first. Stopping takes all three down.
esp_err_t i2s_start_capture();
esp_err_t i2s_stop_capture();

// True once either receiver has dropped a block since capture started. The
// two streams have drifted apart by then and capture has to be restarted.
bool i2s_capture_overflowed();

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

// Same as i2s_read(), from the aux mic's controller. Frame n of each stream
// was sampled at the same instant, as long as both are read in equal lengths.
esp_err_t i2s_read_aux(void* data, size_t len, size_t* bytes_read);