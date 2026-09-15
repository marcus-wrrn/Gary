#pragma once
#include "esp_err.h"
#include "lvgl.h"

#define DISPLAY_H_RES   240
#define DISPLAY_V_RES   320

// Requires spi_init() to have been called first. Also calls lv_init().
esp_err_t display_init();

// path is an LVGL path into the SD card, e.g. "S:plant/eye_240.jpg"
esp_err_t display_show_image(const char* path);

// Orientation/colour check: tall red bar at top-left, wide green bar at bottom-right.
// Red showing as blue means the RGB/BGR order is wrong; bars rotated or in other
// corners mean the axes are swapped or mirrored.
esp_err_t display_show_test_pattern();
