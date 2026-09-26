#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {
    SOUND_DIR_NONE = 0,   // too quiet, or too close to centre to call
    SOUND_DIR_LEFT,
    SOUND_DIR_RIGHT,
} SoundDirection;

void direction_reset();

void direction_process(const int32_t* frames, const int32_t* aux, size_t count);

SoundDirection direction_get();

int direction_get_percent();

bool direction_get_bearing(float* degrees);

// Where the source sits seen from the front, within the unit disc: x from -1 (left) to 1 (right),
// y from -1 (below) to 1 (above), and (0, 0) straight ahead. y stays 0 without the aux mic.
// False while no source is heard.
bool direction_get_position(float* x, float* y);

// "Left", "Right" or "N/A"
const char* direction_name(SoundDirection dir);
