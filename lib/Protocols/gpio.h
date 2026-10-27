#pragma once
#include "config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

// Brings up ADC1 only; each channel is configured by whoever owns it
esp_err_t gpio_init_adc();
esp_err_t gpio_adc_config_channel(adc_channel_t channel);
esp_err_t gpio_adc_create_cali(adc_channel_t channel, adc_cali_handle_t* cali);
esp_err_t gpio_adc_oneshot_read(adc_channel_t channel, int* level);


esp_err_t gpio_init_digital();
