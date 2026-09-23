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

// Where each mic sits, in metres, looking at the board from the front: x runs
// from the left mic towards the right one (the sides direction_get() names)
// and y points ahead, the way the display faces. Only the offsets between
// mics matter, not where the origin is. The aux mic has to sit off the line
// through the other two, or it adds nothing the pair doesn't already know.
#define DIRECTION_LEFT_X        (-0.06f)
#define DIRECTION_LEFT_Y        0.0f
#define DIRECTION_RIGHT_X       0.06f
#define DIRECTION_RIGHT_Y       0.0f
#define DIRECTION_AUX_X         0.0f
#define DIRECTION_AUX_Y         (-0.05f)

#define DIRECTION_LAG_LIMIT     20

#define DIRECTION_FLOOR_DB      (-65.0f)
#define DIRECTION_MIN_CORR      0.4f

// Fraction of each new block's correlation curve folded into the running one.
// Averaging whole curves rather than per-block answers lets a weak but
// consistent peak build up while one-off echo peaks wash out.
#define DIRECTION_SMOOTHING     0.2f
// How far across the source has to point, as a fraction of fully side-on, to
// claim a side, and the fraction below which the claim is dropped. The gap
// between them stops it chattering at the edge.
#define DIRECTION_ENTER         0.3f
#define DIRECTION_EXIT          0.12f
// Minimum time a displayed answer stays put, so a single clap doesn't flash
#define DIRECTION_HOLD_US       (300 * 1000)

// Bounds on the length of the fitted direction vector, which comes out at 1
// for a source level with the mics. It shrinks as the source climbs overhead,
// where the bearing stops meaning much, and grows past 1 when the pair delays
// contradict each other, which is echo or noise rather than a source.
#define DIRECTION_FIT_MIN       0.3f
#define DIRECTION_FIT_MAX       1.5f

#define DIRECTION_ABS(v)        ((v) < 0 ? -(v) : (v))
// Samples a sound can take to cross from one mic to another. |dx| + |dy| is
// never shorter than the true spacing, so the assert below errs safe.
#define DIRECTION_SPAN_SAMPLES(ax, ay, bx, by) \
    ((DIRECTION_ABS((ax) - (bx)) + DIRECTION_ABS((ay) - (by))) * \
     DIRECTION_SAMPLE_RATE / DIRECTION_SOUND_SPEED)

_Static_assert(DIRECTION_SPAN_SAMPLES(DIRECTION_LEFT_X, DIRECTION_LEFT_Y, DIRECTION_RIGHT_X, DIRECTION_RIGHT_Y) + 2 <=
                   DIRECTION_LAG_LIMIT &&
               DIRECTION_SPAN_SAMPLES(DIRECTION_LEFT_X, DIRECTION_LEFT_Y, DIRECTION_AUX_X, DIRECTION_AUX_Y) + 2 <=
                   DIRECTION_LAG_LIMIT &&
               DIRECTION_SPAN_SAMPLES(DIRECTION_RIGHT_X, DIRECTION_RIGHT_Y, DIRECTION_AUX_X, DIRECTION_AUX_Y) + 2 <=
                   DIRECTION_LAG_LIMIT,
               "Mic spacing needs more lags than DIRECTION_LAG_LIMIT allows");

typedef enum {
    MIC_LEFT = 0,
    MIC_RIGHT,
    MIC_AUX,
    MIC_COUNT,
} direction_mic_t;

static const float dir_mic_pos[MIC_COUNT][2] = {
    [MIC_LEFT] = { DIRECTION_LEFT_X, DIRECTION_LEFT_Y },
    [MIC_RIGHT] = { DIRECTION_RIGHT_X, DIRECTION_RIGHT_Y },
    [MIC_AUX] = { DIRECTION_AUX_X, DIRECTION_AUX_Y },
};

// The left/right pair comes first so it can run alone when there's no aux
typedef enum {
    PAIR_LEFT_RIGHT = 0,
    PAIR_LEFT_AUX,
    PAIR_RIGHT_AUX,
    PAIR_COUNT,
} direction_pair_t;

typedef struct {
    direction_mic_t first;
    direction_mic_t second;
    // Added to the measured lag, in samples. The aux stream should start on
    // the same frame as the main one; if a clap straight at the aux mic still
    // shows a steady whole-sample offset on its two pairs, it goes here.
    float trim;
} direction_pair_info_t;

static const direction_pair_info_t dir_pairs[PAIR_COUNT] = {
    [PAIR_LEFT_RIGHT] = { MIC_LEFT, MIC_RIGHT, 0.0f },
    [PAIR_LEFT_AUX] = { MIC_LEFT, MIC_AUX, 0.0f },
    [PAIR_RIGHT_AUX] = { MIC_RIGHT, MIC_AUX, 0.0f },
};

static float dir_signal[MIC_COUNT][DIRECTION_BLOCK];
// Index k holds lag k - DIRECTION_LAG_LIMIT, only the middle 2 * max lag + 1 used
static float dir_corr[PAIR_COUNT][2 * DIRECTION_LAG_LIMIT + 1];
// Largest delay each pair's spacing allows, in samples, and the lags searched
static float dir_max_delay[PAIR_COUNT];
static int dir_max_lag[PAIR_COUNT];

static SoundDirection dir_candidate = SOUND_DIR_NONE;
static int64_t dir_changed_us = 0;

// Written by direction_process() on the sound task and read by the display
static portMUX_TYPE dir_lock = portMUX_INITIALIZER_UNLOCKED;
static SoundDirection dir_current = SOUND_DIR_NONE;
static int dir_percent = 0;
static float dir_bearing = 0.0f;
static bool dir_bearing_valid = false;

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
// lines second[n + k] up with first[n], so it peaks there when the second mic
// hears the sound k samples after the first one. Every lag sums over the same
// span so they compare fairly.
static float direction_correlate(const float* first, const float* second, int lag, int max_lag) {
    float cross = 0.0f;
    float first_energy = 0.0f;
    float second_energy = 0.0f;
    for (int n = max_lag; n < DIRECTION_BLOCK - max_lag; n++) {
        float a = first[n];
        float b = second[n + lag];
        cross += a * b;
        first_energy += a * a;
        second_energy += b * b;
    }
    return cross / (sqrtf(first_energy * second_energy) + 1e-20f);
}

// Where a pair's running curve peaks, in samples with the parabola fit and
// trim applied, and how strongly. False if the peak is too weak to trust.
static bool direction_peak_lag(direction_pair_t pair, float* lag, float* strength) {
    const float* corr = &dir_corr[pair][DIRECTION_LAG_LIMIT];
    int max_lag = dir_max_lag[pair];

    int best = -max_lag;
    for (int k = -max_lag; k <= max_lag; k++) {
        if (corr[k] > corr[best]) {
            best = k;
        }
    }

    float peak = corr[best];
    if (peak < DIRECTION_MIN_CORR) {
        return false;
    }

    // The true delay is rarely a whole number of samples, and with only a few
    // lags to go on the fraction matters. At the ends there is no outer
    // neighbour, so the whole lag stands.
    float offset = 0.0f;
    if (best > -max_lag && best < max_lag) {
        float before = corr[best - 1];
        float after = corr[best + 1];
        float curve = before - 2.0f * peak + after;
        if (curve < 0.0f) {
            offset = 0.5f * (before - after) / curve;
        }
    }
    *lag = (float)best + offset + dir_pairs[pair].trim;
    *strength = peak;
    return true;
}

// Far-field fit: a source along unit vector u reaches mic p (p . u) / c
// sooner than the origin, so the lag of pair (a, b) is (p_a - p_b) . u / c.
// With two or more pairs that's a small linear system in u, solved by least
// squares weighted by how sharp each peak is. Writes the unfitted vector,
// whose length says how well the pairs agreed.
static bool direction_fit(const bool* valid, const float* lags, const float* strengths, float* ux, float* uy) {
    float a11 = 0.0f, a12 = 0.0f, a22 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    int used = 0;
    for (int p = 0; p < PAIR_COUNT; p++) {
        if (!valid[p]) {
            continue;
        }
        const float* pa = dir_mic_pos[dir_pairs[p].first];
        const float* pb = dir_mic_pos[dir_pairs[p].second];
        float dx = pa[0] - pb[0];
        float dy = pa[1] - pb[1];
        float path = lags[p] * DIRECTION_SOUND_SPEED / DIRECTION_SAMPLE_RATE;
        float w = strengths[p];

        a11 += w * dx * dx;
        a12 += w * dx * dy;
        a22 += w * dy * dy;
        b1 += w * dx * path;
        b2 += w * dy * path;
        used++;
    }
    if (used < 2) {
        return false;
    }

    // Measured against the scale of the terms, so a layout with its mics all
    // in a line is caught whatever the spacing
    float det = a11 * a22 - a12 * a12;
    float scale = a11 + a22;
    if (det <= 1e-3f * scale * scale) {
        return false;
    }

    *ux = (a22 * b1 - a12 * b2) / det;
    *uy = (a11 * b2 - a12 * b1) / det;
    return true;
}

void direction_reset() {
    for (int p = 0; p < PAIR_COUNT; p++) {
        const float* pa = dir_mic_pos[dir_pairs[p].first];
        const float* pb = dir_mic_pos[dir_pairs[p].second];
        float spacing = hypotf(pa[0] - pb[0], pa[1] - pb[1]);
        dir_max_delay[p] = spacing * DIRECTION_SAMPLE_RATE / DIRECTION_SOUND_SPEED;
        dir_max_lag[p] = (int)ceilf(dir_max_delay[p]) + 1;
    }

    memset(dir_corr, 0, sizeof(dir_corr));
    dir_candidate = SOUND_DIR_NONE;
    dir_changed_us = esp_timer_get_time();

    taskENTER_CRITICAL(&dir_lock);
    dir_current = SOUND_DIR_NONE;
    dir_percent = 0;
    dir_bearing_valid = false;
    taskEXIT_CRITICAL(&dir_lock);
}

void direction_process(const int32_t* frames, const int32_t* aux, size_t count) {
    if (frames == NULL || count < DIRECTION_BLOCK) {
        return;
    }

    // The mics are mounted so the I2S right slot (1) sits on the left side as
    // seen from the display, so the slots are read into the opposite sides.
    // The aux mic is strapped to its controller's left slot.
    float loudest = direction_prepare(frames, 1, dir_signal[MIC_LEFT]);
    float db = direction_prepare(frames, 0, dir_signal[MIC_RIGHT]);
    if (db > loudest) loudest = db;
    int pair_count = 1;
    if (aux != NULL) {
        db = direction_prepare(aux, 0, dir_signal[MIC_AUX]);
        if (db > loudest) loudest = db;
        pair_count = PAIR_COUNT;
    }
    bool heard = loudest >= DIRECTION_FLOOR_DB;

    bool valid[PAIR_COUNT] = { false };
    float lags[PAIR_COUNT];
    float strengths[PAIR_COUNT];
    for (int p = 0; p < pair_count; p++) {
        const float* first = dir_signal[dir_pairs[p].first];
        const float* second = dir_signal[dir_pairs[p].second];
        int max_lag = dir_max_lag[p];

        // A quiet block pulls the curve towards zero instead of adding to it,
        // so the peak sinks under DIRECTION_MIN_CORR once the sound stops and
        // the answer lapses to none rather than sticking where it was
        for (int k = -max_lag; k <= max_lag; k++) {
            float block = heard ? direction_correlate(first, second, k, max_lag) : 0.0f;
            float* running = &dir_corr[p][k + DIRECTION_LAG_LIMIT];
            *running += (block - *running) * DIRECTION_SMOOTHING;
        }
        valid[p] = direction_peak_lag(p, &lags[p], &strengths[p]);
    }

    // -1 fully right .. +1 fully left, and 0 when there's no usable peak.
    // With a bearing this is how far across it points; without one it falls
    // back on the left/right pair alone, as before the third mic.
    float score = 0.0f;
    float ux, uy;
    bool bearing_valid = false;
    float bearing = 0.0f;
    if (direction_fit(valid, lags, strengths, &ux, &uy)) {
        float length = hypotf(ux, uy);
        if (length >= DIRECTION_FIT_MIN && length <= DIRECTION_FIT_MAX) {
            ux /= length;
            uy /= length;
            bearing = atan2f(ux, uy) * (180.0f / (float)M_PI);
            bearing_valid = true;
            score = -ux;
        }
    } else if (valid[PAIR_LEFT_RIGHT]) {
        score = lags[PAIR_LEFT_RIGHT] / dir_max_delay[PAIR_LEFT_RIGHT];
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
    dir_bearing = bearing;
    dir_bearing_valid = bearing_valid;
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

bool direction_get_bearing(float* degrees) {
    if (degrees == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&dir_lock);
    bool valid = dir_bearing_valid;
    if (valid) {
        *degrees = dir_bearing;
    }
    taskEXIT_CRITICAL(&dir_lock);
    return valid;
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
