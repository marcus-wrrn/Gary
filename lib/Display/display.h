#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include "config.h"

// Requires spi_init() to have been called first. Also calls lv_init().
// Leaves the backlight off; call display_set_brightness() once a frame is on the panel.
esp_err_t display_init();

// 0 turns the backlight off, values above 100 are clamped. Not safe to call from two tasks at once.
esp_err_t display_set_brightness(uint8_t percent);

// path is an LVGL path into the SD card, e.g. "S:plant/eye_240.jpg"
esp_err_t display_show_image(const char* path);

// Orientation/colour check: tall red bar at top-left, wide green bar at bottom-right.
// Red showing as blue means the RGB/BGR order is wrong; bars rotated or in other
// corners mean the axes are swapped or mirrored.
esp_err_t display_show_test_pattern();
