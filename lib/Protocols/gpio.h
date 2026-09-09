#pragma once
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define SSD1306_RST_PIN GPIO_NUM_21
#define VEXT_PIN GPIO_NUM_36
#define TEST_BUTTON_PIN GPIO_NUM_3
#define MOISTURE_SENSOR_CHANNEL ADC_CHANNEL_4

esp_err_t gpio_init_adc();
esp_err_t gpio_adc_oneshot_read(adc_channel_t channel, int* level);


esp_err_t gpio_init_digital();
