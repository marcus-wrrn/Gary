#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Capture format for the mic. The INMP441 clocks 24-bit samples inside 32-bit
// slots, so the bus stays 32 bits wide no matter how little of it carries data.
#define MIC_SAMPLE_RATE     16000
#define MIC_BIT_DEPTH       32

// Bars in the spectrum snapshot, log-spaced across the audible range so the
// crowded low end gets as many as the sparse top octave.
#define MIC_BAND_COUNT      64

esp_err_t play_track(const char* filename);

// Starts and stops draining the I2S RX channel. TX and RX share one clock, so
// play_track() borrows the bus while it runs and hands it back afterwards.
esp_err_t mic_start();
esp_err_t mic_stop();

// True while the mic is draining, for callers deciding whether to poll.
bool mic_is_running();

// Reads one block of frames and folds it into the running level summary, which
// is logged once a second, and into the spectrum snapshot. Blocks until the
// block arrives, so calling this in a loop is what keeps the RX DMA from
// overflowing.
esp_err_t mic_poll();

// Copies the newest spectrum snapshot into bands, one 0-255 level per band,
// lowest frequency first; count must be MIC_BAND_COUNT. Returns
// ESP_ERR_INVALID_STATE and leaves bands untouched until mic_poll() has
// produced a block, and again once the mic stops, so a caller that keeps
// drawing has something to fade out on. Safe to call from another task while
// mic_poll() runs.
esp_err_t mic_get_spectrum(uint8_t* bands, size_t count);
