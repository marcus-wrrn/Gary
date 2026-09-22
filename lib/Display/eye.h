#pragma once
#include "esp_err.h"
#include "display.h"

typedef enum {
    EYE_MODE_IDLE = 0,
    EYE_MODE_FOLLOW_SOUND = 1,
} EyeMode;

esp_err_t display_draw_eye(EyeMode mode);

// Pauses or resumes the look and blink animations where they are. No-op before display_draw_eye().
void display_set_eye_animation(bool play);

// Stops the animations and removes the eye from the screen, leaving it ready to
// be drawn again. No-op if the eye is not on screen.
void display_clear_eye();
