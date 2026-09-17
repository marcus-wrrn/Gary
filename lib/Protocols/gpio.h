#pragma once
#include "config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"

#define MIC_SAMPLE_RATE_HZ 8000

esp_err_t gpio_init_adc();
#ifdef MICROPHONE_OUT_CHANNEL
// Blocks up to timeout_ms for the next frame, samples are raw 12-bit readings
esp_err_t gpio_adc_read_mic(uint16_t* samples, size_t max_samples, size_t* out_samples, uint32_t timeout_ms);
#endif
// Returns the latest moisture sample, ESP_ERR_INVALID_STATE until one has been captured
esp_err_t gpio_adc_read_moisture(int* level);


esp_err_t gpio_init_digital();
