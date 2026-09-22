#include "sound_sphere.h"
#include <math.h>
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
#define SPHERE_REFRESH_MS   40

// Fraction of the remaining distance a bar covers per refresh. Rising fast and
// falling slowly is what makes a transient read as a shape rather than a flicker.
#define SPHERE_RISE         0.55f
#define SPHERE_FALL         0.12f

// lv_line keeps a pointer to its points, so they must outlive the lines
static lv_point_precise_t sphere_points[SPHERE_BAR_COUNT][2];

static struct {
    lv_obj_t* bars[SPHERE_BAR_COUNT];
    lv_timer_t* refresh;
    float level[SPHERE_BAR_COUNT];   // smoothed 0..1 height of each band
    int32_t drawn_len[SPHERE_BAR_COUNT];
} sphere_state;

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

static void sphere_refresh_cb(lv_timer_t* timer) {
    LV_UNUSED(timer);

    // A mic that is stopped or has not filled a block yet leaves the levels
    // falling towards zero, which settles the ring at SPHERE_BAR_MIN.
    uint8_t bands[SPHERE_BAR_COUNT];
    bool have_bands = mic_get_spectrum(bands, SPHERE_BAR_COUNT) == ESP_OK;

    for (size_t i = 0; i < SPHERE_BAR_COUNT; i++) {
        float target = have_bands ? bands[i] / 255.0f : 0.0f;
        float rate = target > sphere_state.level[i] ? SPHERE_RISE : SPHERE_FALL;
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

esp_err_t display_draw_sound_sphere() {
    if (lv_display_get_default() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t* screen = lv_screen_active();
    for (size_t i = 0; i < SPHERE_BAR_COUNT; i++) {
        lv_obj_t* bar = lv_line_create(screen);
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
