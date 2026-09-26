#pragma once
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// define all pins here
// Digital / analog IO
#define SSD1306_RST_PIN         GPIO_NUM_2
#define TEST_BUTTON_PIN         GPIO_NUM_15
#define MOISTURE_SENSOR_CHANNEL ADC_CHANNEL_0   // GPIO1

// I2C (display)
#define I2C_DISPLAY_SDA_PIN     GPIO_NUM_41
#define I2C_DISPLAY_SCL_PIN     GPIO_NUM_42

// I2S (speaker + mic share BCLK/WS)
#define I2S_BCLK_PIN            GPIO_NUM_14
#define I2S_WS_PIN              GPIO_NUM_21
#define I2S_DOUT_PIN            GPIO_NUM_47     // to the amplifier's DIN
#define I2S_DIN_PIN             GPIO_NUM_17     // from the mic's SD

// Third mic on the second I2S controller, a slave clocked by the pair above
#define I2S_DIN2_PIN            GPIO_NUM_16     // from the third mic's SD
#define I2S_BCLK2_PIN           GPIO_NUM_18     // jumpered to I2S_BCLK_PIN
#define I2S_WS2_PIN             GPIO_NUM_5      // jumpered to I2S_WS_PIN


// SDMMC (SD Card) - onboard slot, 1-bit mode only
#define SDMMC_CMD_PIN           GPIO_NUM_38
#define SDMMC_CLK_PIN           GPIO_NUM_39
#define SDMMC_D0_PIN            GPIO_NUM_40

#define SPI_CS_PIN              GPIO_NUM_10
#define SPI_SCK_PIN             GPIO_NUM_12
#define SPI_MOSI_PIN            GPIO_NUM_11
#define SPI_MISO_PIN            GPIO_NUM_13
#define SPI_DISPLAY_DC_PIN      GPIO_NUM_9
#define DISPLAY_RST_PIN         GPIO_NUM_8
