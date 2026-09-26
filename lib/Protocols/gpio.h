#pragma once
#include "config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

esp_err_t gpio_init_adc();
esp_err_t gpio_adc_oneshot_read(adc_channel_t channel, int* level);
esp_err_t gpio_read_thermistor(float* temp_c);


esp_err_t gpio_init_digital();
