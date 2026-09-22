#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "display.h"

// What the bars are driven by. The ring itself is the same in every mode.
typedef enum {
    // One bar per spectrum band, lowest frequency at the top, running clockwise
    SOUND_SPHERE_SPECTRUM = 0,
    // Left half follows the left mic's loudness and right half the right mic's
    SOUND_SPHERE_DIRECTION = 1,
} SoundSphereMode;

esp_err_t display_draw_sound_sphere(SoundSphereMode mode);

// Pauses or resumes the refresh, leaving the bars where they are. No-op before
// display_draw_sound_sphere().
void display_set_sound_sphere_animation(bool play);

void display_clear_sound_sphere();
