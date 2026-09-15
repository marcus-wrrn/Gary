#pragma once
#include "esp_err.h"
#include "display.h"

// Line-drawn eye centred on screen: upper lid, crease, lower lid with lashes, and a pupil
// that keeps looking left and right along a curved path while the upper lid blinks every few
// seconds. Requires display_init() to have
// been called first, and lv_timer_handler() running for the animation.
esp_err_t display_draw_eye();
