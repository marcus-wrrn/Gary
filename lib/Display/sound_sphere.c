#include "sound_sphere.h"
#include <math.h>
#include <string.h>
#include "audio.h"

#define SPHERE_PI           3.14159265f
#define SPHERE_COLOR        0xFFFFFF
#define SPHERE_STROKE       2
#define SPHERE_BAR_COUNT    MIC_BAND_COUNT
#define SPHERE_RADIUS       70   // circle the bars stand on, centre to bar base
#define SPHERE_BAR_MIN      5    // bar length at silence, so the ring stays readable in a quiet room
#define SPHERE_BAR_MAX      44   // SPHERE_RADIUS + this has to stay inside half the narrow axis

// Every refresh redraws most of the ring, which is the expensive part on an SPI
// panel; this is the knob to turn if the display task starts falling behind.
// Matches the mic, which finishes a spectrum every 256 samples at 16 kHz.
#define SPHERE_REFRESH_MS   16

// Fraction of the remaining distance a bar covers per SPHERE_RATE_REF_MS. Rising
// fast and falling slowly is what makes a transient read as a shape rather than
// a flicker. They are rescaled to the refresh period, so changing
// SPHERE_REFRESH_MS changes smoothness without changing how the bars move.
#define SPHERE_RATE_REF_MS  40.0f
#define SPHERE_RISE         0.55f
#define SPHERE_FALL         0.12f

// lv_line keeps a pointer to its points, so they must outlive the lines
static lv_point_precise_t sphere_points[SPHERE_BAR_COUNT][2];

static struct {
    // Every bar hangs off this, so tearing the ring down is one delete
    lv_obj_t* root;
    lv_obj_t* bars[SPHERE_BAR_COUNT];
    lv_timer_t* refresh;
    SoundSphereMode mode;
    float level[SPHERE_BAR_COUNT];   // smoothed 0..1 height of each band
    int32_t drawn_len[SPHERE_BAR_COUNT];
    float rise;   // SPHERE_RISE and SPHERE_FALL rescaled to one refresh
    float fall;
} sphere_state;

// Covering fraction r per reference period leaves (1 - r) behind, so n shorter
// steps covering the same ground each leave (1 - r)^(1/n)
static float sphere_rate_per_refresh(float rate) {
    return 1.0f - powf(1.0f - rate, SPHERE_REFRESH_MS / SPHERE_RATE_REF_MS);
}

// Lays one bar along its spoke. The points are kept relative to the object and
// the object is moved to the bar's own bounding box, so a redraw only dirties
// that box; absolute points would make every bar's box reach back to the corner
// of the screen and turn each frame into a full-screen repaint.
static void sphere_place_bar(size_t i, int32_t length) {
    float angle = -SPHERE_PI / 2 + 2 * SPHERE_PI * (float)i / (float)SPHERE_BAR_COUNT;
    float dx = cosf(angle);
    float dy = sinf(angle);
    float cx = DISPLAY_H_RES / 2.0f;
    float cy = DISPLAY_V_RES / 2.0f;

    int32_t base_x = lroundf(cx + dx * SPHERE_RADIUS);
    int32_t base_y = lroundf(cy + dy * SPHERE_RADIUS);
    int32_t tip_x = lroundf(cx + dx * (SPHERE_RADIUS + length));
    int32_t tip_y = lroundf(cy + dy * (SPHERE_RADIUS + length));

    int32_t left = base_x < tip_x ? base_x : tip_x;
    int32_t top = base_y < tip_y ? base_y : tip_y;

    sphere_points[i][0] = (lv_point_precise_t){base_x - left, base_y - top};
    sphere_points[i][1] = (lv_point_precise_t){tip_x - left, tip_y - top};

    lv_obj_set_pos(sphere_state.bars[i], left, top);
    lv_line_set_points(sphere_state.bars[i], sphere_points[i], 2);
    sphere_state.drawn_len[i] = length;
}

// Fills targets with the 0..1 height each bar is heading for. A mic that is
// stopped or has not filled a block yet leaves them at zero, so the levels fall
// and the ring settles at SPHERE_BAR_MIN.
static void sphere_read_targets(float* targets) {
    memset(targets, 0, sizeof(float) * SPHERE_BAR_COUNT);

    if (sphere_state.mode == SOUND_SPHERE_SPECTRUM) {
        uint8_t bands[SPHERE_BAR_COUNT];
        if (mic_get_spectrum(bands, SPHERE_BAR_COUNT) == ESP_OK) {
            for (size_t i = 0; i < SPHERE_BAR_COUNT; i++) {
                targets[i] = bands[i] / 255.0f;
            }
        }
        return;
    }

    uint8_t left;
    uint8_t right;
    if (mic_get_levels(&left, &right) != ESP_OK) {
        return;
    }

    // Bar 0 points straight up and they run clockwise, so the first half of
    // the ring is the right side and the second half the left. The two bars on
    // the vertical axis belong to neither side and show the mean.
    const size_t half = SPHERE_BAR_COUNT / 2;
    for (size_t i = 0; i < SPHERE_BAR_COUNT; i++) {
        if (i == 0 || i == half) {
            targets[i] = (left + right) / (2.0f * 255.0f);
        } else if (i < half) {
            targets[i] = right / 255.0f;
        } else {
            targets[i] = left / 255.0f;
        }
    }
}

static void sphere_refresh_cb(lv_timer_t* timer) {
    LV_UNUSED(timer);

    float targets[SPHERE_BAR_COUNT];
    sphere_read_targets(targets);

    for (size_t i = 0; i < SPHERE_BAR_COUNT; i++) {
        float target = targets[i];
        float rate = target > sphere_state.level[i] ? sphere_state.rise : sphere_state.fall;
        sphere_state.level[i] += (target - sphere_state.level[i]) * rate;

        int32_t length = SPHERE_BAR_MIN +
                         lroundf(sphere_state.level[i] * (SPHERE_BAR_MAX - SPHERE_BAR_MIN));
        // Levels keep drifting by fractions of a pixel long after the bar has
        // settled, so redraws are skipped until the rounded length actually moves.
        if (length != sphere_state.drawn_len[i]) {
            sphere_place_bar(i, length);
        }
    }
}

esp_err_t display_draw_sound_sphere(SoundSphereMode mode) {
    if (lv_display_get_default() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sphere_state.root != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sphere_state.root = lv_obj_create(lv_screen_active());
    if (sphere_state.root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(sphere_state.root);
    lv_obj_remove_flag(sphere_state.root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sphere_state.root, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_set_pos(sphere_state.root, 0, 0);

    sphere_state.mode = mode;
    sphere_state.rise = sphere_rate_per_refresh(SPHERE_RISE);
    sphere_state.fall = sphere_rate_per_refresh(SPHERE_FALL);

    for (size_t i = 0; i < SPHERE_BAR_COUNT; i++) {
        lv_obj_t* bar = lv_line_create(sphere_state.root);
        lv_obj_set_style_line_width(bar, SPHERE_STROKE, 0);
        lv_obj_set_style_line_color(bar, lv_color_hex(SPHERE_COLOR), 0);
        lv_obj_set_style_line_rounded(bar, true, 0);

        sphere_state.bars[i] = bar;
        sphere_state.level[i] = 0.0f;
        // Nothing is drawn yet, so no length can match and the first placement stands
        sphere_state.drawn_len[i] = -1;
        sphere_place_bar(i, SPHERE_BAR_MIN);
    }

    // Levels are pulled from the mic rather than pushed, so this is a plain
    // timer instead of an lv_anim: there is no end state to animate towards.
    sphere_state.refresh = lv_timer_create(sphere_refresh_cb, SPHERE_REFRESH_MS, NULL);
    if (sphere_state.refresh == NULL) {
        lv_obj_delete(sphere_state.root);
        sphere_state.root = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void display_set_sound_sphere_animation(bool play) {
    if (sphere_state.refresh == NULL) {
        return;
    }
    if (play) {
        lv_timer_resume(sphere_state.refresh);
    } else {
        lv_timer_pause(sphere_state.refresh);
    }
}

void display_clear_sound_sphere() {
    if (sphere_state.root == NULL) {
        return;
    }

    // The timer is not an object and so would outlive the bars, and its next
    // tick would place them through pointers that have already been freed.
    lv_timer_delete(sphere_state.refresh);
    lv_obj_delete(sphere_state.root);
    memset(&sphere_state, 0, sizeof(sphere_state));
}
