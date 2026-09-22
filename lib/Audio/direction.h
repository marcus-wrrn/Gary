#pragma once
#include <stddef.h>
#include <stdint.h>

// Which side a sound is coming from, judged by which mic hears it first. The
// two slots share one word clock, so their samples line up exactly and the
// delay between them comes straight from cross-correlating the pair. Doesn't
// care whether the mics are matched in gain, but does need them far enough
// apart for the delay to span a few samples (DIRECTION_MIC_SPACING_M).

typedef enum {
    SOUND_DIR_NONE = 0,   // too quiet, or too close to centre to call
    SOUND_DIR_LEFT,
    SOUND_DIR_RIGHT,
} SoundDirection;

// Forgets the running estimate, so the next call to direction_get() says
// SOUND_DIR_NONE until enough new blocks agree on a side.
void direction_reset();

// Folds one block of interleaved frames (left slot first, as read off I2S)
// into the running estimate. Blocks shorter than the transform are ignored.
void direction_process(const int32_t* frames, size_t count);

// The current settled estimate. Safe to call from another task while
// direction_process() runs.
SoundDirection direction_get();

// How far towards the side direction_get() reports the source sits, 0..100:
// 0 is dead ahead and 100 is fully side-on, where the delay is the largest the
// mic spacing allows. Follows the source every block, unlike the side, which
// holds. 0 while the side is SOUND_DIR_NONE.
int direction_get_percent();

// "Left", "Right" or "N/A"
const char* direction_name(SoundDirection dir);
