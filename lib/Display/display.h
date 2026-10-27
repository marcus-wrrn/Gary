#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include "config.h"

esp_err_t display_init();

esp_err_t display_set_brightness(uint8_t percent);

esp_err_t display_set_backlight(bool on);

// path is an LVGL path into the SD card, e.g. "S:plant/eye_240.jpg"
esp_err_t display_show_image(const char* path);

esp_err_t display_show_test_pattern();
