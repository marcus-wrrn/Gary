#include "freertos/FreeRTOS.h"
#include "ssd1306.h"
#include "esp_log.h"
#include "esp_err.h"
#include "gpio.h"
#include "i2c.h"
#include "string.h"

#define _min(x1, x2) ((x1) < (x2) ? (x1) : (x2))
#define _max(x1, x2) ((x1) > (x2) ? (x1) : (x2))

static const char* TAG = "SSD1306";

static const uint8_t display_width = 128;
static const uint8_t display_height = 64;
static uint16_t display_buffer_size = (uint16_t)display_width * (uint16_t)display_height / 8;

static const DISPLAY_GEOMETRY geometry = GEOMETRY_128_64;
static DISPLAY_ANGLE rotate_angle = ANGLE_0_DEGREE;
static DISPLAY_TEXT_ALIGN text_alignment = TEXT_ALIGN_LEFT;
static DISPLAY_COLOR color = WHITE;
static const uint8_t *font_data = ArialMT_Plain_10;
static const FontTableLookupFunction font_table_lookup_function = DefaultFontTableLookup;

static uint16_t log_buffer_size;
static uint16_t log_buffer_filled;
static uint16_t log_buffer_line;
static uint16_t log_buffer_max_lines;
static const char* log_buffer;


#define STR_BUFFER_SIZE 1024
static char str_buffer[STR_BUFFER_SIZE];

uint8_t* buffer = NULL;
#ifdef DISPLAY_DOUBLE_BUFFER
uint8_t* buffer_back;
#endif

i2c_master_dev_handle_t device_handle;

esp_err_t send_command(uint8_t command) {
    uint8_t buffer[] = { 0x80, command };
    return i2c_master_transmit(device_handle, buffer, sizeof(buffer), 1000);

}

esp_err_t send_init_commands() {
    if (geometry == GEOMETRY_RAWMODE) {
        return ESP_OK;
    }
    esp_err_t err = send_command(SSD1306_DISPLAYOFF);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to turn off display");

    err = send_command(SSD1306_SETDISPLAYCLOCKDIV);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set display clock divide ratio");

    err = send_command(0xF0); // Increase speed of the display max ~96Hz
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set display clock divide ratio value");

    err = send_command(SSD1306_SETMULTIPLEX);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set multiplex ratio");

    err = send_command(display_height - 1);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set multiplex ratio value");

    err = send_command(SSD1306_SETDISPLAYOFFSET);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set display offset");

    err = send_command(0x00);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set display offset value");

    err = send_command(SSD1306_SETSTARTLINE);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set start line");

    err = send_command(SSD1306_CHARGEPUMP);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set charge pump");

    err = send_command(0x14);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to enable charge pump");

    err = send_command(SSD1306_MEMORYMODE);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set memory addressing mode");

    err = send_command(0x00);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set horizontal addressing mode");

    err = send_command(SSD1306_SEGREMAP | 0x01);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set segment re-map");

    err = send_command(SSD1306_COMSCANDEC);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set COM output scan direction");

    err = send_command(SSD1306_SETCOMPINS);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set COM pins hardware configuration");

    err = send_command(0x12);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set COM pins hardware configuration value");

    err = send_command(SSD1306_SETCONTRAST);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set contrast control");

    err = send_command(0xCF);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set contrast control value");

    err = send_command(SSD1306_SETPRECHARGE);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set pre-charge period");

    err = send_command(0xF1);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set pre-charge period value");

    err = send_command(SSD1306_SETVCOMDETECT); // additionally needed to lower the contrast
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set VCOMH deselect level");

    err = send_command(0x40); // 0x40 default, to lower the contrast, put 0
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set VCOMH deselect level value");

    err = send_command(SSD1306_DISPLAYALLON_RESUME);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to resume display from RAM");

    err = send_command(SSD1306_NORMALDISPLAY);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to set normal display mode");

    err = send_command(0x2e); // stop scroll
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to stop scroll");

    err = send_command(SSD1306_DISPLAYON);
    if (err != ESP_OK)  ESP_LOGE(TAG, "Unable to turn on display");

    vTaskDelay(pdMS_TO_TICKS(1000));
    return err;
}

void clear_display(void) {
    if (buffer == NULL || display_buffer_size <= 0) {
        return;
    }
    memset(buffer, 0, display_buffer_size);
}

void display_reset(void) {
    clear_display();
#ifdef DISPLAY_DOUBLE_BUFFER
    memset(buffer_back, 1, display_buffer_size);
#endif

}


esp_err_t display_draw(void) {
#ifdef DISPLAY_DOUBLE_BUFFER
    ESP_LOGI(TAG, "Drawing Buffer");
    uint8_t min_bound_y = UINT8_MAX;
    uint8_t max_bound_y = 0;

    uint8_t min_bound_x = UINT8_MAX;
    uint8_t max_bound_x = 0;
    uint8_t x, y;

    // Pages are 8 pixels tall so display_height needs to be divided by 8
    for (y = 0; y < (display_height / 8); y += 1) {
        for (x = 0; x < display_width; x += 1) {
            uint16_t pos = x + y * display_width;
            if (buffer[pos] != buffer_back[pos]) {
                min_bound_y = _min(min_bound_y, y);
                max_bound_y = _max(max_bound_y, y);
                min_bound_x = _min(min_bound_x, x);
                max_bound_x = _max(max_bound_x, x);
            }
            buffer_back[pos] = buffer[pos];
        }
    }

    if (min_bound_y == UINT8_MAX) {
        ESP_LOGI(TAG, "Min Y bound exceeded");
        return ESP_OK;
    };

    // Set column
    esp_err_t err = send_command(SSD1306_COLUMNADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set column address");
        return err;
    }
    err = send_command(min_bound_x);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set minimum x bound");
        return err;
    }
    err = send_command(max_bound_x);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set maximum x bound");
        return err;
    }

    // set row (page)
    err = send_command(SSD1306_PAGEADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set page address");
        return err;
    }
    err = send_command(min_bound_y);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set minimum y bound");
        return err;
    }
    err = send_command(max_bound_y);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set maximum y bound");
        return err;
    }

    // write to the display
    uint8_t k[17] = {0};
    uint8_t i = 0;
    for (y = min_bound_y; y <= max_bound_y; y += 1) {
        for (x = min_bound_x; x <= max_bound_x; x += 1) {
            if (i == 0) {
                k[i] = 0x40;
                i += 1;
            }
            k[i] = buffer[x + y * display_width];
            i += 1;
            if (i == 17) {
                err = i2c_master_transmit(device_handle, k, i, -1);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Unable to transmit display buffer chunk");
                    return err;
                }
                i = 0;
            }
        }
    }

    if (i != 0) {
        err = i2c_master_transmit(device_handle, k, i, -1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Unable to transmit final display buffer chunk");
            return err;
        }
    }
    ESP_LOGI(TAG, "Finished display");
    return ESP_OK;
}
#else
    esp_err_t err = send_command(SSD1306_COLUMNADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set column address");
        return err;
    }
    err = send_command(0x0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set minimum x bound");
        return err;
    }
    err = send_command((display_width - 1));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set maximum x bound");
        return err;
    }
    err = send_command(SSD1306_PAGEADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set page address");
        return err;
    }
    err = send_command(0x0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set minimum page bound");
        return err;
    }
    if (geometry == GEOMETRY_128_64) {
        err = send_command(0x7);
    } else if (geometry == GEOMETRY_128_32) {
        err = send_command(0x3);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set maximum page bound");
        return err;
    }

    uint8_t control_byte = 0x40;
    i2c_master_transmit_multi_buffer_info_t segments[2] = {
        { .write_buffer = &control_byte, .buffer_size = 1 },
        { .write_buffer = buffer, .buffer_size = display_buffer_size }
    };

    err = i2c_master_multi_buffer_transmit(device_handle, segments, 2, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to transmit display buffer");
        return err;
    }
    return ESP_OK;
}
#endif

esp_err_t display_set_pixel(int16_t x, int16_t y) {
    if (x >= 0 && x < display_width && y >= 0 && y < display_height) {
        switch (color) {
            case WHITE: buffer[x + (y >> 3) * display_width] |= (1 << (y & 7)); break;
            case BLACK: buffer[x + (y >> 3) * display_width] &= ~(1 << (y & 7)); break;
            case INVERSE: buffer[x + (y >> 3) * display_width] ^= (1 << (y & 7)); break;
        }
        return ESP_OK;
    }
    return ESP_ERR_INVALID_SIZE;
}

esp_err_t utf8ascii(char* str_buffer, char* str, uint16_t str_len) {
    if (str_len >= STR_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Input String exceeds maximum buffer size");
        return ESP_ERR_INVALID_SIZE;
    }
    uint16_t k = 0;
    for (uint16_t i = 0; i < str_len; i++) {
        char c = (font_table_lookup_function)(str[i]);
        if (c != 0) {
            str_buffer[k] = c;
            k += 1;
        }
    }
    str_buffer[k] = 0;
    return ESP_OK;
}

void display_draw_internal(int16_t x_move, int16_t y_move, int16_t width, int16_t height, const uint8_t *data, uint16_t offset, uint16_t bytes_in_data) {
    if (width < 0 || height < 0) return;
    if (y_move + height < 0 || y_move > display_height) return;
    if (x_move + width < 0 || x_move > display_width) return;

    uint8_t raster_height = 1 + ((height - 1) >> 3);
    int8_t y_offset = y_move & 7;

    bytes_in_data = bytes_in_data == 0 ? width * raster_height : bytes_in_data;

    int16_t init_y_move = y_move;
    int8_t init_y_offset = y_offset;

    for (uint16_t i = 0; i < bytes_in_data; i += 1) {
        if (i % raster_height == 0) {
            y_move = init_y_move;
            y_offset = init_y_offset;
        }

        uint8_t current_byte = *(data + offset + i);
        int16_t x_pos = x_move + (i / raster_height);
        int16_t y_pos = ((y_move >> 3) + (i % raster_height)) * display_width;

        int16_t data_pos = x_pos + y_pos;

        if (data_pos >= 0 && data_pos < display_buffer_size && x_pos >= 0 && x_pos < display_width) {
            if (y_offset >= 0) {
                switch (color) {
                    case WHITE: buffer[data_pos] |= current_byte << y_offset; break;
                    case BLACK: buffer[data_pos] &= ~(current_byte << y_offset); break;
                    case INVERSE: buffer[data_pos] ^= current_byte << y_offset; break;
                }

                if (data_pos < (display_buffer_size - display_width)) {
                    switch (color) {
                        case WHITE: buffer[data_pos + display_width] |= current_byte >> (8 - y_offset); break;
                        case BLACK: buffer[data_pos + display_width] &= ~(current_byte >> (8 - y_offset)); break;
                        case INVERSE: buffer[data_pos + display_width] ^= current_byte >> (8 - y_offset); break;
                    }
                }
            } else {
                y_offset = -y_offset;
                switch (color) {
                    case WHITE: buffer[data_pos] |= current_byte >> y_offset; break;
                    case BLACK: buffer[data_pos] &= ~(current_byte >> y_offset); break;
                    case INVERSE: buffer[data_pos] ^= current_byte >> y_offset; break;
                }
                y_move -= 8;
                y_offset = 8 - y_offset;
            }
        }
    }
}

void display_draw_string_internal(int16_t x_move, int16_t y_move, char* text, uint16_t text_len, uint16_t text_width) {
    uint8_t text_height = font_data[SSD1306_HEIGHT_POS];
    uint8_t first_char = font_data[SSD1306_FIRST_CHAR_POS];
    uint16_t size_of_jump_table = font_data[SSD1306_CHAR_NUM_POS] * SSD1306_JUMPTABLE_BYTES;

    uint16_t cursor_x = 0;
    uint16_t cursor_y = 0;

    switch (text_alignment) {
        case TEXT_ALIGN_CENTER_BOTH:
            y_move -= text_height >> 1;
        case TEXT_ALIGN_CENTER:
            x_move -= text_width >> 1;
            break;
        case TEXT_ALIGN_RIGHT:
            x_move -= text_width;
            break;
        case TEXT_ALIGN_LEFT:
            break;
    }

    if (x_move + text_width < 0 || x_move > display_width) {
        ESP_LOGE(TAG, "Text out of range of display width x < 0 || x > display_width");
        return;
    } 
    if (y_move + text_height < 0 || y_move > display_height) {
        ESP_LOGE(TAG, "Text out of range of display height y < 0 || y > display_height");
        return;
    }

    for (uint16_t i = 0; i < text_len; i += 1) {
        int16_t x_pos = x_move + cursor_x;
        int16_t y_pos = y_move + cursor_y;

        uint8_t code = text[i];
        if (code >= first_char) {
            uint8_t char_code = code - first_char;

            uint8_t msb_jump_to_char = font_data[SSD1306_JUMPTABLE_START + char_code * SSD1306_JUMPTABLE_BYTES];
            uint8_t lsb_jump_to_char = font_data[SSD1306_JUMPTABLE_START + char_code * SSD1306_JUMPTABLE_BYTES + SSD1306_JUMPTABLE_LSB];
            uint8_t char_byte_size = font_data[SSD1306_JUMPTABLE_START + char_code * SSD1306_JUMPTABLE_BYTES + SSD1306_JUMPTABLE_SIZE];
            uint8_t current_char_width = font_data[SSD1306_JUMPTABLE_START + char_code * SSD1306_JUMPTABLE_BYTES + SSD1306_JUMPTABLE_WIDTH];

            if (!(msb_jump_to_char == 255 && lsb_jump_to_char == 255)) {
                uint16_t char_data_pos = SSD1306_JUMPTABLE_START + size_of_jump_table + ((msb_jump_to_char << 8) + lsb_jump_to_char);
                display_draw_internal(x_pos, y_pos, current_char_width, text_height, font_data, char_data_pos, char_byte_size);
            }
            cursor_x += current_char_width;
        }
    }
}

uint16_t get_string_width(const char* text, uint16_t len) {
    uint16_t first_char = font_data[SSD1306_FIRST_CHAR_POS];

    uint16_t string_width = 0;
    uint16_t max_width = 0;

    while (len) {
        len -= 1;
        string_width += font_data[SSD1306_JUMPTABLE_START + (text[len] - first_char) * SSD1306_JUMPTABLE_BYTES + SSD1306_JUMPTABLE_WIDTH];
        if (text[len] == 10) {
            max_width = _max(max_width, string_width);
            string_width = 0;
        }
    }
    return _max(max_width, string_width);
}

void display_draw_string(int16_t x_move, int16_t y_move, char* str, uint16_t str_len) {
    uint16_t line_height = font_data[SSD1306_HEIGHT_POS];
    
    esp_err_t res = utf8ascii(str_buffer, str, str_len);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Unable to to draw string to display");
        return;
    }
    uint16_t y_offset = 0;
    uint16_t line = 0;
    char* text_part = strtok(str_buffer, "\n");
    
    while (text_part != NULL) {
        ESP_LOGI(TAG, "Drawing: %s", text_part);
        uint16_t str_length = strlen(text_part);
        uint16_t string_width = get_string_width(text_part, str_length);
        display_draw_string_internal(x_move, y_move - y_offset + line * line_height, text_part, str_length, string_width);
        line += 1;
        text_part = strtok(NULL, "\n");
    }
    
}

char DefaultFontTableLookup(const uint8_t ch) {
    // UTF-8 to font table index converter
    // Code form http://playground.arduino.cc/Main/Utf8ascii
    static uint8_t LASTCHAR;
    if (ch < 128) {
        LASTCHAR = 0;
        return ch;
    }
    uint8_t last = LASTCHAR;
    LASTCHAR = ch;

    switch (last) {
        case 0xC2: return (uint8_t) ch;
        case 0xC3: return (uint8_t) (ch | 0xC0);
        case 0x82: if (ch == 0xAC) return (uint8_t) 0x80;
    }

    return (uint8_t) 0;
}

esp_err_t display_init() {
    // Toggle RST pin
    gpio_set_level(SSD1306_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(SSD1306_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(SSD1306_RST_PIN, 1);
    vTaskDelay(1);

    log_buffer_size = 0;
    log_buffer_filled = 0;
    log_buffer_line = 0;
    log_buffer_max_lines = 0;
    log_buffer = NULL;

    esp_err_t err = i2c_add_device_to_bus(&device_handle, SSD1306_ADDRESS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to add device to bus");
        return err;
    }

    if (buffer == NULL) {
        buffer = (uint8_t*) malloc((sizeof(uint8_t) * display_buffer_size));
    }

    if (!buffer) {
        ESP_LOGE(TAG, "Not enough memory to create display");
        return ESP_ERR_NO_MEM;
    }

    #ifdef DISPLAY_DOUBLE_BUFFER
    if (buffer_back == NULL) {
        buffer_back = (uint8_t*) malloc((sizeof(uint8_t) * display_buffer_size));
    }

    if (!buffer_back) {
        ESP_LOGE(TAG, "Not enough memory to create back buffer");
        return ESP_ERR_NO_MEM;
    }
    #endif

    err = send_init_commands();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to complete initialization flow");
        return err;
    }
    display_reset();
    return err;
}