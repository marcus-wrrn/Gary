#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "display.h"

// Ring of radial bars centred on screen, one per band of the mic's spectrum:
// the lowest band sits at the top of the circle and the rest run clockwise up
// to the highest, each bar growing outwards with how loud its band is. Bars
// rise quickly and fall slowly, so a sound leaves a shape behind for a moment
// instead of flickering. Requires display_init() to have been called first, and
// lv_timer_handler() running for the refresh; the bars sink to their resting
// length whenever the mic is not listening.
esp_err_t display_draw_sound_sphere();

// Pauses or resumes the refresh, leaving the bars where they are. No-op before
// display_draw_sound_sphere().
void display_set_sound_sphere_animation(bool play);
