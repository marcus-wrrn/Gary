#pragma once
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// define all pins here
// Digital / analog IO
#define TEST_BUTTON_PIN         GPIO_NUM_15
#define MOISTURE_SENSOR_CHANNEL ADC_CHANNEL_0   // GPIO1
#define THERMISTOR_CHANNEL      ADC_CHANNEL_5   // GPIO6
#define LIGHT_SENSOR_CHANNEL    ADC_CHANNEL_3   // GPIO4, 3V3 -> LDR -> GPIO4 -> 10k -> GND

// I2C (display)
#define I2C_DISPLAY_SDA_PIN     GPIO_NUM_41
#define I2C_DISPLAY_SCL_PIN     GPIO_NUM_42

// SHT31 temp/humidity (SEN0331), shares the I2C bus above
#define SHT31_ALERT_PIN         GPIO_NUM_7      // active high
#define SHT31_RST_PIN           GPIO_NUM_3      // active low
#define SHT31_I2C_ADDR          0x44            // ADDR pin low
#define SHT31_I2C_ADDR_ALT      0x45            // ADDR pin high, tried if the first is silent

// I2S (speaker + mic share BCLK/WS)
#define I2S_BCLK_PIN            GPIO_NUM_14
#define I2S_WS_PIN              GPIO_NUM_21
#define I2S_DOUT_PIN            GPIO_NUM_47     // to the amplifier's DIN
#define I2S_DIN_PIN             GPIO_NUM_17     // from the mic's SD

// Third mic on the second I2S controller, a slave clocked by the pair above.
// Left unconnected so the controller is never set up and only the pair is read.
#define I2S_DIN2_PIN            GPIO_NUM_NC     // disabled; GPIO_NUM_16 to bring the third mic back
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
#define DISPLAY_BL_PIN          GPIO_NUM_2      // to the module's LED pin, high = on

// I2S tone playback
#define I2S_TONE_SAMPLE_RATE    48000
#define I2S_TONE_BIT_DEPTH      32
#define I2S_READ_TIMEOUT_MS     1000

// SD Card
#define SD_CARD_MOUNT_POINT     "/sdcard"
#define MAX_PATH_SIZE           128

// NTC thermistor divider: 3V3 -> THERMISTOR_SERIES_OHMS -> GPIO6 -> NTC -> GND
#define THERMISTOR_SUPPLY_MV    3300
#define THERMISTOR_SERIES_OHMS  10000.0f
#define THERMISTOR_NOMINAL_OHMS 10000.0f        // resistance at 25C
#define THERMISTOR_NOMINAL_C    25.0f
#define THERMISTOR_BETA         3950.0f

// Mic capture format. The INMP441 clocks 24-bit samples inside 32-bit slots,
// so the bus stays 32 bits wide no matter how little of it carries data.
#define MIC_SAMPLE_RATE         16000
#define MIC_BIT_DEPTH           32

// Mic positions in metres, facing the board: x is left-to-right, y is up.
// Only the offsets between mics matter. The aux mic must sit off the line
// through the other two, or it adds no information.
#define DIRECTION_LEFT_X        (-0.026f)
#define DIRECTION_LEFT_Y        0.0f
#define DIRECTION_RIGHT_X       0.026f
#define DIRECTION_RIGHT_Y       0.0f
#define DIRECTION_AUX_X         0.0f
#define DIRECTION_AUX_Y         (-0.11f)

// Display module resolution/orientation
#define DISPLAY_H_RES           240
#define DISPLAY_V_RES           320
// Set to 1 to flip the image top to bottom, e.g. when the module is mounted upside down
#define DISPLAY_FLIP_VERTICAL   1

// Backlight PWM. 5 kHz is well above visible flicker and leaves room for 10-bit duty.
#define DISPLAY_BL_PWM_FREQ_HZ  5000
#define DISPLAY_BL_DEFAULT_PCT  100
