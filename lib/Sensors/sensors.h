#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

// Needs gpio_init_adc() and i2c_init() first. Configures the moisture, light
// and thermistor ADC channels, then the SHT31. A missing SHT31 is logged but
// not returned as an error, so the analog sensors keep working without it;
// sensors_read_sht31() reports ESP_ERR_INVALID_STATE in that case.
//
// The microphones are not handled here, the sound task owns them through
// mic_start() in audio.h.
esp_err_t sensors_init();

// Raw 12-bit ADC counts, 0-4095
esp_err_t sensors_read_moisture(int* level);
esp_err_t sensors_read_light(int* level);

esp_err_t sensors_read_thermistor(float* temp_c);
esp_err_t sensors_read_sht31(float* temp_c, float* humidity_pct);
