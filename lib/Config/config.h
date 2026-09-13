#pragma once
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// define all pins here
//#define HELTEC_BOARD

#ifdef HELTEC_BOARD
// Digital / analog IO
#define SSD1306_RST_PIN         GPIO_NUM_21
#define VEXT_PIN                GPIO_NUM_36
#define TEST_BUTTON_PIN         GPIO_NUM_3
#define MOISTURE_SENSOR_CHANNEL ADC_CHANNEL_4

// I2C (display)
#define I2C_DISPLAY_SDA_PIN     GPIO_NUM_17
#define I2C_DISPLAY_SCL_PIN     GPIO_NUM_18

// I2S (speaker)
#define I2S_BCLK_PIN            GPIO_NUM_4
#define I2S_WS_PIN              GPIO_NUM_6
#define I2S_DIN_PIN             GPIO_NUM_7

// SPI (SD card)
#define SPI_MISO_PIN            GPIO_NUM_37
#define SPI_MOSI_PIN            GPIO_NUM_35
#define SPI_SCLK_PIN            GPIO_NUM_36
#define SPI_CS_PIN              GPIO_NUM_34
#else

#define SSD1306_RST_PIN         GPIO_NUM_21
#define TEST_BUTTON_PIN         GPIO_NUM_3
#define MOISTURE_SENSOR_CHANNEL ADC_CHANNEL_3

// I2S (speaker)
#define I2S_BCLK_PIN            GPIO_NUM_14
#define I2S_WS_PIN              GPIO_NUM_21
#define I2S_DIN_PIN             GPIO_NUM_47

// SDMMC (SD Card) - onboard slot, 1-bit mode only
#define SDMMC_CMD_PIN           GPIO_NUM_38
#define SDMMC_CLK_PIN           GPIO_NUM_39
#define SDMMC_D0_PIN            GPIO_NUM_40

#endif