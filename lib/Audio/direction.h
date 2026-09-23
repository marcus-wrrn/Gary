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

// "Left", "Right" or "N/A"
const char* direction_name(SoundDirection dir);
