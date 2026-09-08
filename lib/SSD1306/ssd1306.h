#pragma once
#include "ssd1306_fonts.h"

/// Code converted from HeltecV3 project from the Heltec_Display.h/.c files
/// https://github.com/HelTecAutomation/Heltec_ESP32


#define SSD1306_ADDRESS 0x3C

#ifndef DISPLAY_REDUCE_MEMORY
#define DISPLAY_DOUBLE_BUFFER
#endif

#define SSD1306_JUMPTABLE_BYTES     4
#define SSD1306_JUMPTABLE_LSB       1
#define SSD1306_JUMPTABLE_SIZE      2
#define SSD1306_JUMPTABLE_WIDTH     3  // number of characters in the jumptable (stored in font buffer)
#define SSD1306_JUMPTABLE_START     4

// Display Commands
#define SSD1306_CHARGEPUMP              0x8D
#define SSD1306_COLUMNADDR              0x21
#define SSD1306_COMSCANDEC              0xC8
#define SSD1306_COMSCANINC              0xC0
#define SSD1306_DISPLAYALLON            0xA5
#define SSD1306_DISPLAYALLON_RESUME     0xA4
#define SSD1306_DISPLAYOFF              0xAE
#define SSD1306_DISPLAYON               0xAF
#define SSD1306_EXTERNALVCC             0x1
#define SSD1306_INVERTDISPLAY           0xA7
#define SSD1306_MEMORYMODE              0x20
#define SSD1306_NORMALDISPLAY           0xA6
#define SSD1306_PAGEADDR                0x22
#define SSD1306_SEGREMAP                0xA0
#define SSD1306_SETCOMPINS              0xDA
#define SSD1306_SETCONTRAST             0x81
#define SSD1306_SETDISPLAYCLOCKDIV      0xD5
#define SSD1306_SETDISPLAYOFFSET        0xD3
#define SSD1306_SETHIGHCOLUMN           0x10
#define SSD1306_SETLOWCOLUMN            0x00
#define SSD1306_SETMULTIPLEX            0xA8
#define SSD1306_SETPRECHARGE            0xD9
#define SSD1306_SETSEGMENTREMAP         0xA1
#define SSD1306_SETSTARTLINE            0x40
#define SSD1306_SETVCOMDETECT           0xDB
#define SSD1306_SWITCHCAPVCC            0x2

#define SSD1306_WIDTH_POS               0
#define SSD1306_HEIGHT_POS              1
#define SSD1306_FIRST_CHAR_POS          2
#define SSD1306_CHAR_NUM_POS            3

typedef enum DISPLAY_BUFFER {
    BLACK_BUFFER = 0,
    COLOR_BUFFER = 1
} DISPLAY_BUFFER;

typedef enum DISPLAY_TEXT_ALIGN {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_RIGHT = 1,
    TEXT_ALIGN_CENTER = 2,
    TEXT_ALIGN_CENTER_BOTH = 3
} DISPLAY_TEXT_ALIGN;

typedef enum DISPLAY_GEOMETRY {
    GEOMETRY_128_64 = 0,
    GEOMETRY_128_32 = 1,
    GEOMETRY_200_200 = 2,
    GEOMETRY_250_122 = 3,
    GEOMETRY_296_128 = 4,
    GEOMETRY_RAWMODE = 5,
    GEOMETRY_64_32 = 6
} DISPLAY_GEOMETRY;

typedef enum DISPLAY_ANGLE {
    ANGLE_0_DEGREE = 0,
    ANGLE_90_DEGREE = 1,
    ANGLE_180_DEGREE = 2,
    ANGLE_270_DEGREE = 3
} DISPLAY_ANGLE;

typedef enum DISPLAY_COLOR {
    BLACK = 0,
    WHITE = 1,
    INVERSE = 2
} DISPLAY_COLOR;

typedef char (*FontTableLookupFunction)(const uint8_t ch);
char DefaultFontTableLookup(const uint8_t ch);


esp_err_t display_init();
void display_reset();
void display_draw_string(int16_t x_move, int16_t y_move, char* str, uint16_t str_len);
esp_err_t display_set_pixel(int16_t x, int16_t y);
esp_err_t display_draw(void);
void clear_display(void);
