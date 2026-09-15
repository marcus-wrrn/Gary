#pragma once
#include "esp_err.h"
#include "display.h"

// Line-drawn eye centred on screen: upper lid, crease, lower lid with lashes, and a pupil
// that keeps looking left and right along a curved path. Every few seconds, once the pupil reaches
// an end of its path, it stops while the inner lid line blinks down onto the bottom lid. Requires display_init() to have been called first,
// and lv_timer_handler() running for the animation.
esp_err_t display_draw_eye();

// Pauses or resumes the look and blink animations where they are. No-op before display_draw_eye().
void display_set_eye_animation(bool play);
