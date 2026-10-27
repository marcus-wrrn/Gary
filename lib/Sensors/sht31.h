#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

// Needs i2c_init() first. Pulses the reset line, finds the sensor at
// SHT31_I2C_ADDR or SHT31_I2C_ADDR_ALT and soft resets it.
esp_err_t sht31_init();

// One high repeatability single-shot measurement, about 15 ms. Either output
// may be NULL if only the other is wanted.
esp_err_t sht31_read(float* temp_c, float* humidity_pct);

// Hardware reset through SHT31_RST_PIN, for a sensor that stops answering
esp_err_t sht31_reset();

// Level of the ALERT pin, high once a configured alert limit is crossed
bool sht31_alert_active();
