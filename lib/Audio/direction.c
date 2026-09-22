#include "direction.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

// Matches the mic's block, so one block is one estimate: 16 ms at 16 kHz
#define DIRECTION_BLOCK         256
#define DIRECTION_SAMPLE_RATE   16000.0f
#define DIRECTION_SOUND_SPEED   343.0f
// Same meaning as MIC_FULL_SCALE in audio.c
#define DIRECTION_FULL_SCALE    8388608.0f

#define DIRECTION_MIC_SPACING_M 0.12f
#define DIRECTION_MAX_DELAY \
    (DIRECTION_MIC_SPACING_M * DIRECTION_SAMPLE_RATE / DIRECTION_SOUND_SPEED)
#define DIRECTION_MAX_LAG       ((int)ceilf(DIRECTION_MAX_DELAY) + 1)

#define DIRECTION_LAG_LIMIT     20

#define DIRECTION_FLOOR_DB      (-65.0f)
#define DIRECTION_MIN_CORR      0.4f
#define DIRECTION_LAG_TRIM      0.0f

// Fraction of each new block's correlation curve folded into the running one.
// Averaging whole curves rather than per-block answers lets a weak but
// consistent peak build up while one-off echo peaks wash out.
#define DIRECTION_SMOOTHING     0.2f
// Smoothed lag, as a fraction of DIRECTION_MAX_DELAY, needed to claim a
// side, and the fraction below which the claim is dropped. The gap between
// them stops it chattering at the edge.
#define DIRECTION_ENTER         0.3f
#define DIRECTION_EXIT          0.12f
// Minimum time a displayed answer stays put, so a single clap doesn't flash
#define DIRECTION_HOLD_US       (300 * 1000)

_Static_assert(DIRECTION_MIC_SPACING_M * DIRECTION_SAMPLE_RATE / DIRECTION_SOUND_SPEED + 2 <=
                   DIRECTION_LAG_LIMIT,
               "Mic spacing needs more lags than DIRECTION_LAG_LIMIT allows");

static float dir_left[DIRECTION_BLOCK];
static float dir_right[DIRECTION_BLOCK];
// Index k holds lag k - DIRECTION_LAG_LIMIT, only the middle 2 * max lag + 1 used
static float dir_corr[2 * DIRECTION_LAG_LIMIT + 1];

static SoundDirection dir_candidate = SOUND_DIR_NONE;
static int64_t dir_changed_us = 0;

// Written by direction_process() on the sound task and read by the display
static portMUX_TYPE dir_lock = portMUX_INITIALIZER_UNLOCKED;
static SoundDirection dir_current = SOUND_DIR_NONE;
static int dir_percent = 0;

// Copies one slot out, drops its mean and takes the first difference. The
// difference tilts the spectrum towards the highs, which narrows the
// correlation peak; low rumble otherwise spreads it across every lag.
// Returns the slot's RMS in dB relative to full scale.
static float direction_prepare(const int32_t* frames, size_t slot, float* out) {
    float mean = 0.0f;
    for (size_t i = 0; i < DIRECTION_BLOCK; i++) {
        out[i] = (float)(frames[i * 2 + slot] >> 8) / DIRECTION_FULL_SCALE;
        mean += out[i];
    }
    mean /= (float)DIRECTION_BLOCK;

    float sum_sq = 0.0f;
    float prev = out[0] - mean;
    for (size_t i = 0; i < DIRECTION_BLOCK; i++) {
        float x = out[i] - mean;
        sum_sq += x * x;
        out[i] = x - prev;
        prev = x;
    }
    return 10.0f * log10f(sum_sq / (float)DIRECTION_BLOCK + 1e-18f);
}

// Normalised cross-correlation of the block at lag k, -1..1. A positive lag
// lines right[n + k] up with left[n], so it peaks there when the right mic
// hears the sound k samples after the left one. Every lag sums over the same
// span so they compare fairly.
static float direction_correlate(int lag, int max_lag) {
    float cross = 0.0f;
    float left_energy = 0.0f;
    float right_energy = 0.0f;
    for (int n = max_lag; n < DIRECTION_BLOCK - max_lag; n++) {
        float l = dir_left[n];
        float r = dir_right[n + lag];
        cross += l * r;
        left_energy += l * l;
        right_energy += r * r;
    }
    return cross / (sqrtf(left_energy * right_energy) + 1e-20f);
}

// Where the running curve peaks, in samples with the parabola fit applied,
// or false if the peak is too weak to trust
static bool direction_peak_lag(int max_lag, float* lag) {
    int best = -max_lag;
    for (int k = -max_lag; k <= max_lag; k++) {
        if (dir_corr[k + DIRECTION_LAG_LIMIT] > dir_corr[best + DIRECTION_LAG_LIMIT]) {
            best = k;
        }
    }

    float peak = dir_corr[best + DIRECTION_LAG_LIMIT];
    if (peak < DIRECTION_MIN_CORR) {
        return false;
    }

    // The true delay is rarely a whole number of samples, and with only a few
    // lags to go on the fraction matters. At the ends there is no outer
    // neighbour, so the whole lag stands.
    float offset = 0.0f;
    if (best > -max_lag && best < max_lag) {
        float before = dir_corr[best - 1 + DIRECTION_LAG_LIMIT];
        float after = dir_corr[best + 1 + DIRECTION_LAG_LIMIT];
        float curve = before - 2.0f * peak + after;
        if (curve < 0.0f) {
            offset = 0.5f * (before - after) / curve;
        }
    }
    *lag = (float)best + offset + DIRECTION_LAG_TRIM;
    return true;
}

void direction_reset() {
    memset(dir_corr, 0, sizeof(dir_corr));
    dir_candidate = SOUND_DIR_NONE;
    dir_changed_us = esp_timer_get_time();

    taskENTER_CRITICAL(&dir_lock);
    dir_current = SOUND_DIR_NONE;
    dir_percent = 0;
    taskEXIT_CRITICAL(&dir_lock);
}

void direction_process(const int32_t* frames, size_t count) {
    if (frames == NULL || count < DIRECTION_BLOCK) {
        return;
    }
    const int max_lag = DIRECTION_MAX_LAG;

    // The mics are mounted so the I2S right slot (1) sits on the left side as
    // seen from the display, so the slots are read into the opposite sides
    float left_db = direction_prepare(frames, 1, dir_left);
    float right_db = direction_prepare(frames, 0, dir_right);
    bool heard = (left_db > right_db ? left_db : right_db) >= DIRECTION_FLOOR_DB;

    // A quiet block pulls the curve towards zero instead of adding to it, so
    // the peak sinks under DIRECTION_MIN_CORR once the sound stops and the
    // answer lapses to none rather than sticking on its side
    for (int k = -max_lag; k <= max_lag; k++) {
        float block = heard ? direction_correlate(k, max_lag) : 0.0f;
        float* running = &dir_corr[k + DIRECTION_LAG_LIMIT];
        *running += (block - *running) * DIRECTION_SMOOTHING;
    }

    // -1 fully right .. +1 fully left, and 0 when there's no usable peak
    float score = 0.0f;
    float lag;
    if (direction_peak_lag(max_lag, &lag)) {
        score = lag / DIRECTION_MAX_DELAY;
    }

    SoundDirection next = dir_candidate;
    if (score > DIRECTION_ENTER) {
        next = SOUND_DIR_LEFT;
    } else if (score < -DIRECTION_ENTER) {
        next = SOUND_DIR_RIGHT;
    } else if (dir_candidate == SOUND_DIR_LEFT && score < DIRECTION_EXIT) {
        next = SOUND_DIR_NONE;
    } else if (dir_candidate == SOUND_DIR_RIGHT && score > -DIRECTION_EXIT) {
        next = SOUND_DIR_NONE;
    }

    int64_t now = esp_timer_get_time();
    if (next != dir_candidate && now - dir_changed_us >= DIRECTION_HOLD_US) {
        dir_candidate = next;
        dir_changed_us = now;
    }

    // Measured towards the side being shown, so while the hold keeps a side up
    // after the source has drifted across, this bottoms out at 0 rather than
    // pointing the other way. The fraction fit can carry the lag slightly past
    // the physical limit, hence the cap.
    float towards = 0.0f;
    if (dir_candidate == SOUND_DIR_LEFT) {
        towards = score;
    } else if (dir_candidate == SOUND_DIR_RIGHT) {
        towards = -score;
    }
    if (towards < 0.0f) towards = 0.0f;
    if (towards > 1.0f) towards = 1.0f;
    int percent = (int)lroundf(towards * 100.0f);

    taskENTER_CRITICAL(&dir_lock);
    dir_current = dir_candidate;
    dir_percent = percent;
    taskEXIT_CRITICAL(&dir_lock);
}

SoundDirection direction_get() {
    taskENTER_CRITICAL(&dir_lock);
    SoundDirection dir = dir_current;
    taskEXIT_CRITICAL(&dir_lock);
    return dir;
}

int direction_get_percent() {
    taskENTER_CRITICAL(&dir_lock);
    int percent = dir_percent;
    taskEXIT_CRITICAL(&dir_lock);
    return percent;
}

const char* direction_name(SoundDirection dir) {
    switch (dir) {
    case SOUND_DIR_LEFT:
        return "Left";
    case SOUND_DIR_RIGHT:
        return "Right";
    case SOUND_DIR_NONE:
        break;
    }
    return "N/A";
}
