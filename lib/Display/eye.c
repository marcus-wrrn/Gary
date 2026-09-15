#include "eye.h"
#include <math.h>

#define EYE_PI            3.14159265f
#define EYE_COLOR         0xFFFFFF
#define EYE_STROKE        3
#define EYE_HALF_WIDTH    100   // corner to centre, horizontally
#define EYE_UPPER_LID_H   55    // how far each lid bows away from the corner-to-corner line
#define EYE_CREASE_H      38
#define EYE_LOWER_LID_H   35
#define EYE_LID_PTS       40
#define EYE_PUPIL_R       16
#define EYE_LASH_COUNT    10
#define EYE_LASH_GAP      6
#define EYE_LASH_LEN      10

// Pupil path: swings EYE_LOOK_X either side of centre, dipping EYE_LOOK_SAG px lower in the middle
#define EYE_LOOK_X        50
#define EYE_LOOK_SAG      8
#define EYE_LOOK_MS       1500
#define EYE_LOOK_PAUSE_MS 400

// Blink: the inner lid line (EYE_LID_CREASE) bows down until it lies on the bottom lid, then lifts
// again. The top arc stays put, so the moving line never crosses it.
#define EYE_BLINK_MS       200
#define EYE_BLINK_HOLD_MS  60
#define EYE_BLINK_PAUSE_MS 3000

enum { EYE_LID_UPPER, EYE_LID_CREASE, EYE_LID_LOWER, EYE_LID_COUNT };

// lv_line keeps a pointer to its points, so they must outlive the lines
static lv_point_precise_t eye_lids[EYE_LID_COUNT][EYE_LID_PTS];
static lv_point_precise_t eye_lashes[EYE_LASH_COUNT][2];

// Shared between the pupil and blink animations so each can re-clip the pupil against the lid
static struct {
    lv_obj_t* blink_lid;
    lv_obj_t* pupil_clip;
    lv_obj_t* pupil;
    lv_anim_t* look_anim;
    lv_anim_t* blink_anim;
    float pupil_x;
    float pupil_y;
    float lid_h;  // signed: positive bows up, negative bows down
} eye_state;

static lv_obj_t* eye_create_line(lv_point_precise_t* pts, uint32_t count) {
    lv_obj_t* line = lv_line_create(lv_screen_active());
    lv_line_set_points(line, pts, count);
    lv_obj_set_style_line_width(line, EYE_STROKE, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(EYE_COLOR), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static void eye_draw_lid(lv_point_precise_t* pts, float cx, float cy, float height, bool bulge_up,
                             float* out_cy, float* out_r, float* out_half_rad) {
    float r = (EYE_HALF_WIDTH * EYE_HALF_WIDTH + height * height) / (2 * height);
    float half = asinf(EYE_HALF_WIDTH / r);
    // An upward bow is the top of a circle centred below the eye, and vice versa
    float circle_cy = bulge_up ? cy + (r - height) : cy - (r - height);
    float mid = bulge_up ? 3 * EYE_PI / 2 : EYE_PI / 2;

    for (int i = 0; i < EYE_LID_PTS; i++) {
        float a = mid - half + 2 * half * i / (EYE_LID_PTS - 1);
        pts[i].x = lroundf(cx + r * cosf(a));
        pts[i].y = lroundf(circle_cy + r * sinf(a));
    }
    eye_create_line(pts, EYE_LID_PTS);

    if (out_cy) *out_cy = circle_cy;
    if (out_r) *out_r = r;
    if (out_half_rad) *out_half_rad = half;
}

// Screen y of a corner-to-corner arc at horizontal offset dx from the centre. Sampled by x rather
// than angle so the height can pass through zero (a flat line) while animating.
static float eye_lid_y(float dx, float cy, float height) {
    float h = fabsf(height);
    if (h < 0.5f) {
        return cy;
    }
    if (dx > EYE_HALF_WIDTH) dx = EYE_HALF_WIDTH;
    if (dx < -EYE_HALF_WIDTH) dx = -EYE_HALF_WIDTH;
    float r = (EYE_HALF_WIDTH * EYE_HALF_WIDTH + h * h) / (2 * h);
    float bow = sqrtf(r * r - dx * dx) - (r - h);
    return height > 0 ? cy - bow : cy + bow;
}

// Hides whatever part of the pupil sits above the blinking lid by moving the clip container's top edge
// to the lid. The edge is flat, so it follows the lid at the pupil's centre only.
// The container hugs the visible part of the pupil, since moving it redraws both its old and new area.
static void eye_update_pupil() {
    float cx = DISPLAY_H_RES / 2.0f;
    float cy = DISPLAY_V_RES / 2.0f;
    int32_t left = lroundf(eye_state.pupil_x - EYE_PUPIL_R);
    int32_t pupil_top = lroundf(eye_state.pupil_y - EYE_PUPIL_R);
    int32_t bottom = pupil_top + EYE_PUPIL_R * 2;
    int32_t top = lroundf(eye_lid_y(eye_state.pupil_x - cx, cy, eye_state.lid_h));

    if (top < pupil_top) top = pupil_top;
    if (top > bottom) top = bottom;

    lv_obj_set_pos(eye_state.pupil_clip, left, top);
    lv_obj_set_size(eye_state.pupil_clip, EYE_PUPIL_R * 2, bottom - top);
    lv_obj_set_pos(eye_state.pupil, 0, pupil_top - top);
}

static void eye_pupil_anim_cb(void* var, int32_t v) {
    LV_UNUSED(var);
    float t = v / 1000.0f;
    eye_state.pupil_x = DISPLAY_H_RES / 2.0f + EYE_LOOK_X * t;
    eye_state.pupil_y = DISPLAY_V_RES / 2.0f + EYE_LOOK_SAG * (1 - t * t);
    eye_update_pupil();
}

// v runs from 0 (open) to 1000 (closed, lying on the bottom lid)
static void eye_blink_anim_cb(void* var, int32_t v) {
    float t = v / 1000.0f;
    float cx = DISPLAY_H_RES / 2.0f;
    float cy = DISPLAY_V_RES / 2.0f;
    lv_point_precise_t* pts = eye_lids[EYE_LID_CREASE];

    eye_state.lid_h = EYE_CREASE_H + (-EYE_LOWER_LID_H - EYE_CREASE_H) * t;
    for (int i = 0; i < EYE_LID_PTS; i++) {
        float dx = -EYE_HALF_WIDTH + 2.0f * EYE_HALF_WIDTH * i / (EYE_LID_PTS - 1);
        pts[i].x = lroundf(cx + dx);
        pts[i].y = lroundf(eye_lid_y(dx, cy, eye_state.lid_h));
    }
    // Re-setting the points makes the line recompute its size and redraw
    lv_line_set_points((lv_obj_t*)var, pts, EYE_LID_PTS);
    eye_update_pupil();
}

esp_err_t display_draw_eye() {
    if (lv_display_get_default() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t* screen = lv_screen_active();
    float cx = DISPLAY_H_RES / 2.0f;
    float cy = DISPLAY_V_RES / 2.0f;

    eye_draw_lid(eye_lids[EYE_LID_UPPER], cx, cy, EYE_UPPER_LID_H, true, NULL, NULL, NULL);

    float lower_cy, lower_r, lower_half;
    eye_draw_lid(eye_lids[EYE_LID_LOWER], cx, cy, EYE_LOWER_LID_H, false, &lower_cy, &lower_r, &lower_half);

    // Children are clipped to their parent, so the pupil lives in a transparent box whose top edge tracks the lid
    lv_obj_t* pupil_clip = lv_obj_create(screen);
    lv_obj_remove_style_all(pupil_clip);
    lv_obj_remove_flag(pupil_clip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* pupil = lv_obj_create(pupil_clip);
    lv_obj_remove_style_all(pupil);
    lv_obj_set_size(pupil, EYE_PUPIL_R * 2, EYE_PUPIL_R * 2);
    lv_obj_set_style_radius(pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pupil, lv_color_hex(EYE_COLOR), 0);
    lv_obj_set_style_bg_opa(pupil, LV_OPA_COVER, 0);

    eye_state.pupil_clip = pupil_clip;
    eye_state.pupil = pupil;
    eye_state.blink_lid = eye_create_line(eye_lids[EYE_LID_CREASE], 0);
    eye_state.lid_h = EYE_CREASE_H;
    eye_state.pupil_x = cx - EYE_LOOK_X;
    eye_state.pupil_y = cy;
    eye_blink_anim_cb(eye_state.blink_lid, 0);
    eye_pupil_anim_cb(pupil, -1000);

    lv_anim_t blink;
    lv_anim_init(&blink);
    lv_anim_set_var(&blink, eye_state.blink_lid);
    lv_anim_set_exec_cb(&blink, eye_blink_anim_cb);
    lv_anim_set_values(&blink, 0, 1000);
    lv_anim_set_duration(&blink, EYE_BLINK_MS);
    lv_anim_set_reverse_duration(&blink, EYE_BLINK_MS);
    lv_anim_set_reverse_delay(&blink, EYE_BLINK_HOLD_MS);
    lv_anim_set_delay(&blink, EYE_BLINK_PAUSE_MS);
    lv_anim_set_repeat_delay(&blink, EYE_BLINK_PAUSE_MS);
    lv_anim_set_repeat_count(&blink, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&blink, lv_anim_path_ease_in_out);
    eye_state.blink_anim = lv_anim_start(&blink);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, pupil);
    lv_anim_set_exec_cb(&anim, eye_pupil_anim_cb);
    lv_anim_set_values(&anim, -1000, 1000);
    lv_anim_set_duration(&anim, EYE_LOOK_MS);
    lv_anim_set_reverse_duration(&anim, EYE_LOOK_MS);
    lv_anim_set_reverse_delay(&anim, EYE_LOOK_PAUSE_MS);
    lv_anim_set_repeat_delay(&anim, EYE_LOOK_PAUSE_MS);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    eye_state.look_anim = lv_anim_start(&anim);

    // Lashes point straight out from the lower lid's circle, spread along most of the lid
    float span = lower_half * 0.85f;
    for (int i = 0; i < EYE_LASH_COUNT; i++) {
        float a = EYE_PI / 2 - span + 2 * span * i / (EYE_LASH_COUNT - 1);
        float dx = cosf(a);
        float dy = sinf(a);
        float inner = lower_r + EYE_LASH_GAP;
        float outer = inner + EYE_LASH_LEN;
        eye_lashes[i][0] = (lv_point_precise_t){lroundf(cx + dx * inner), lroundf(lower_cy + dy * inner)};
        eye_lashes[i][1] = (lv_point_precise_t){lroundf(cx + dx * outer), lroundf(lower_cy + dy * outer)};
        eye_create_line(eye_lashes[i], 2);
    }
    return ESP_OK;
}

void display_set_eye_animation(bool play) {
    // Both animations repeat forever, so the handles from lv_anim_start() stay valid
    lv_anim_t* anims[] = {eye_state.look_anim, eye_state.blink_anim};
    for (size_t i = 0; i < sizeof(anims) / sizeof(anims[0]); i++) {
        if (anims[i] == NULL) {
            continue;
        }
        if (play) {
            lv_anim_resume(anims[i]);
        } else {
            lv_anim_pause(anims[i]);
        }
    }
}
