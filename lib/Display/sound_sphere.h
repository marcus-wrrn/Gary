#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "display.h"

esp_err_t display_draw_sound_sphere();

// Pauses or resumes the refresh, leaving the bars where they are. No-op before
// display_draw_sound_sphere().
void display_set_sound_sphere_animation(bool play);

void display_clear_sound_sphere();
