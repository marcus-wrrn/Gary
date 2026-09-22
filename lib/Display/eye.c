#include "eye.h"
#include <math.h>
#include <string.h>
#include "direction.h"

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
// again. The top arc stays put, so the moving line never crosses it. Blinks only start once the pupil
// has reached either end of its path, and the pupil waits there until the blink has finished.
#define EYE_BLINK_DOWN_MS  500   // closing
#define EYE_BLINK_HOLD_MS  560   // held closed
#define EYE_BLINK_UP_MS    500   // opening
#define EYE_BLINK_PAUSE_MS 1500  // minimum pupil travel time (legs plus pauses) between blinks


#define EYE_FOLLOW_TICK_MS      33     // about 30 fps; each step only redraws the pupil's own box
#define EYE_FOLLOW_OMEGA        11.0f  // spring stiffness in rad/s; settles in about 4 / this seconds
#define EYE_FOLLOW_DEADBAND     0.2f   // change in gaze needed before the eye glances again
#define EYE_FOLLOW_DWELL_MS     500    // minimum time between glances
#define EYE_FOLLOW_LINGER_MS    1200   // how long the eye keeps looking after the sound goes
#define EYE_FOLLOW_REACH        0.35f  // gaze for a source only just off centre; side-on reaches 1

enum { EYE_LID_UPPER, EYE_LID_CREASE, EYE_LID_LOWER, EYE_LID_COUNT };

// Every part of the eye hangs off this, so tearing the eye down is one delete
// rather than a list of objects that has to be kept in step with the drawing
static lv_obj_t* eye_root = NULL;

// lv_line keeps a pointer to its points, so they must outlive the lines
static lv_point_precise_t eye_lids[EYE_LID_COUNT][EYE_LID_PTS];
static lv_point_precise_t eye_lashes[EYE_LASH_COUNT][2];

// Shared between the pupil and blink animations so each can re-clip the pupil against the lid
static struct {
    EyeMode mode;
    lv_obj_t* blink_lid;
    lv_obj_t* pupil_clip;
    lv_obj_t* pupil;
    int32_t look_to;         // end the current pupil leg heads for: -1000 (left) or 1000 (right)
    uint32_t since_blink_ms; // pupil travel time since the last blink
    float pupil_x;
    float pupil_y;
    float lid_h;  // signed: positive bows up, negative bows down

    // EYE_MODE_FOLLOW_SOUND only
    lv_timer_t* follow;
    float gaze;          // where the pupil is now
    float gaze_vel;      // per second
    float gaze_target;   // where it is gliding to
    uint32_t glance_ms;  // tick of the last change to gaze_target
    uint32_t heard_ms;   // tick of the last block with a side to look at
    uint32_t step_ms;    // tick of the last spring step
} eye_state;

static lv_obj_t* eye_create_line(lv_point_precise_t* pts, uint32_t count) {
    lv_obj_t* line = lv_line_create(eye_root);
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

// t runs from -1 (left end of the path) to 1 (right end)
static void eye_place_pupil(float t) {
    eye_state.pupil_x = DISPLAY_H_RES / 2.0f + EYE_LOOK_X * t;
    eye_state.pupil_y = DISPLAY_V_RES / 2.0f + EYE_LOOK_SAG * (1 - t * t);
    eye_update_pupil();
}

static void eye_pupil_anim_cb(void* var, int32_t v) {
    LV_UNUSED(var);
    eye_place_pupil(v / 1000.0f);
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

static void eye_start_look(uint32_t delay_ms);

static void eye_blink_done_cb(lv_anim_t* a) {
    LV_UNUSED(a);
    eye_start_look(0);
}

// One close-hold-open cycle; the pupil's next leg is started from its completion
static void eye_start_blink() {
    lv_anim_t blink;
    lv_anim_init(&blink);
    lv_anim_set_var(&blink, eye_state.blink_lid);
    lv_anim_set_exec_cb(&blink, eye_blink_anim_cb);
    lv_anim_set_values(&blink, 0, 1000);
    lv_anim_set_duration(&blink, EYE_BLINK_DOWN_MS);
    lv_anim_set_reverse_duration(&blink, EYE_BLINK_UP_MS);
    lv_anim_set_reverse_delay(&blink, EYE_BLINK_HOLD_MS);
    lv_anim_set_path_cb(&blink, lv_anim_path_linear);
    lv_anim_set_completed_cb(&blink, eye_blink_done_cb);
    lv_anim_start(&blink);
}

// Runs when the pupil reaches an end of its path: blink there if one is due, otherwise head back
static void eye_look_done_cb(lv_anim_t* a) {
    LV_UNUSED(a);
    eye_state.look_to = -eye_state.look_to;
    eye_state.since_blink_ms += EYE_LOOK_MS + EYE_LOOK_PAUSE_MS;
    if (eye_state.since_blink_ms >= EYE_BLINK_PAUSE_MS) {
        eye_state.since_blink_ms = 0;
        eye_start_blink();
    } else {
        eye_start_look(EYE_LOOK_PAUSE_MS);
    }
}

// A single leg from the current end of the pupil path to eye_state.look_to
static void eye_start_look(uint32_t delay_ms) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, eye_state.pupil);
    lv_anim_set_exec_cb(&anim, eye_pupil_anim_cb);
    lv_anim_set_values(&anim, -eye_state.look_to, eye_state.look_to);
    lv_anim_set_duration(&anim, EYE_LOOK_MS);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim, eye_look_done_cb);
    lv_anim_start(&anim);
}

// Gaze towards the side the sound comes from, or 0 for none. A side always gets at least
// EYE_FOLLOW_REACH, so a source only just off centre still gets a glance that reads as deliberate.
static float eye_sound_gaze(SoundDirection* dir) {
    *dir = direction_get();
    if (*dir == SOUND_DIR_NONE) {
        return 0.0f;
    }
    float reach = EYE_FOLLOW_REACH + (1.0f - EYE_FOLLOW_REACH) * direction_get_percent() / 100.0f;
    return *dir == SOUND_DIR_LEFT ? -reach : reach;
}

static void eye_follow_cb(lv_timer_t* timer) {
    LV_UNUSED(timer);
    uint32_t now = lv_tick_get();
    float dt = lv_tick_diff(now, eye_state.step_ms) / 1000.0f;
    eye_state.step_ms = now;
    // A late tick, e.g. after the animation was paused, would otherwise fling the pupil
    if (dt > 0.1f) dt = 0.1f;

    SoundDirection dir;
    float wanted = eye_sound_gaze(&dir);
    if (dir != SOUND_DIR_NONE) {
        eye_state.heard_ms = now;
    } else if (lv_tick_diff(now, eye_state.heard_ms) < EYE_FOLLOW_LINGER_MS) {
        // Keep looking at where the sound was for a moment before drifting back
        wanted = eye_state.gaze_target;
    }

    if (fabsf(wanted - eye_state.gaze_target) >= EYE_FOLLOW_DEADBAND &&
        lv_tick_diff(now, eye_state.glance_ms) >= EYE_FOLLOW_DWELL_MS) {
        eye_state.gaze_target = wanted;
        eye_state.glance_ms = now;
    }

    // Semi-implicit Euler, stable while OMEGA * dt stays well under 1
    const float w = EYE_FOLLOW_OMEGA;
    float accel = w * w * (eye_state.gaze_target - eye_state.gaze) - 2.0f * w * eye_state.gaze_vel;
    eye_state.gaze_vel += accel * dt;
    eye_state.gaze += eye_state.gaze_vel * dt;
    if (eye_state.gaze > 1.0f) eye_state.gaze = 1.0f;
    if (eye_state.gaze < -1.0f) eye_state.gaze = -1.0f;

    // The spring creeps by fractions of a pixel long after it has settled, and moving the clip box
    // redraws it, so the pupil is only placed when it would land on a different pixel
    float x = DISPLAY_H_RES / 2.0f + EYE_LOOK_X * eye_state.gaze;
    float y = DISPLAY_V_RES / 2.0f + EYE_LOOK_SAG * (1 - eye_state.gaze * eye_state.gaze);
    if (lroundf(x) != lroundf(eye_state.pupil_x) || lroundf(y) != lroundf(eye_state.pupil_y)) {
        eye_place_pupil(eye_state.gaze);
    }
}

static esp_err_t eye_start_follow() {
    uint32_t now = lv_tick_get();
    eye_state.gaze = 0.0f;
    eye_state.gaze_vel = 0.0f;
    eye_state.gaze_target = 0.0f;
    eye_state.glance_ms = now;
    eye_state.heard_ms = now - EYE_FOLLOW_LINGER_MS;
    eye_state.step_ms = now;
    eye_place_pupil(0.0f);

    // Blinking is left out here: the eye stays open while it listens
    eye_state.follow = lv_timer_create(eye_follow_cb, EYE_FOLLOW_TICK_MS, NULL);
    if (eye_state.follow == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t display_draw_eye(EyeMode mode) {
    if (lv_display_get_default() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (eye_root != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    eye_root = lv_obj_create(lv_screen_active());
    if (eye_root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(eye_root);
    lv_obj_remove_flag(eye_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(eye_root, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_set_pos(eye_root, 0, 0);

    float cx = DISPLAY_H_RES / 2.0f;
    float cy = DISPLAY_V_RES / 2.0f;

    eye_draw_lid(eye_lids[EYE_LID_UPPER], cx, cy, EYE_UPPER_LID_H, true, NULL, NULL, NULL);

    float lower_cy, lower_r, lower_half;
    eye_draw_lid(eye_lids[EYE_LID_LOWER], cx, cy, EYE_LOWER_LID_H, false, &lower_cy, &lower_r, &lower_half);

    // Children are clipped to their parent, so the pupil lives in a transparent box whose top edge tracks the lid
    lv_obj_t* pupil_clip = lv_obj_create(eye_root);
    lv_obj_remove_style_all(pupil_clip);
    lv_obj_remove_flag(pupil_clip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* pupil = lv_obj_create(pupil_clip);
    lv_obj_remove_style_all(pupil);
    lv_obj_set_size(pupil, EYE_PUPIL_R * 2, EYE_PUPIL_R * 2);
    lv_obj_set_style_radius(pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pupil, lv_color_hex(EYE_COLOR), 0);
    lv_obj_set_style_bg_opa(pupil, LV_OPA_COVER, 0);

    eye_state.mode = mode;
    eye_state.pupil_clip = pupil_clip;
    eye_state.pupil = pupil;
    eye_state.blink_lid = eye_create_line(eye_lids[EYE_LID_CREASE], 0);
    eye_state.lid_h = EYE_CREASE_H;
    eye_state.pupil_x = cx - EYE_LOOK_X;
    eye_state.pupil_y = cy;
    eye_blink_anim_cb(eye_state.blink_lid, 0);

    if (mode == EYE_MODE_FOLLOW_SOUND) {
        if (eye_start_follow() != ESP_OK) {
            display_clear_eye();
            return ESP_ERR_NO_MEM;
        }
    } else {
        eye_pupil_anim_cb(pupil, -1000);
        // The pupil and blink animations chain into each other, so exactly one of them runs at a time
        eye_state.look_to = 1000;
        eye_state.since_blink_ms = 0;
        eye_start_look(0);
    }

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
    if (eye_state.pupil == NULL) {
        return;
    }
    // Each leg and blink is a fresh one-shot animation, so look up whichever is currently running
    lv_anim_t* anims[] = {
        lv_anim_get(eye_state.pupil, eye_pupil_anim_cb),
        lv_anim_get(eye_state.blink_lid, eye_blink_anim_cb),
    };
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

    if (eye_state.follow != NULL) {
        if (play) {
            lv_timer_resume(eye_state.follow);
        } else {
            lv_timer_pause(eye_state.follow);
        }
    }
    // The spring measures time between steps, so a long pause would read as one huge step
    eye_state.step_ms = lv_tick_get();
}

void display_clear_eye() {
    if (eye_root == NULL) {
        return;
    }

    // The animations chain into each other, so they are stopped before the
    // objects go: a completion callback firing mid-delete would start the next
    // leg on a pointer that no longer means anything.
    lv_anim_delete(eye_state.pupil, eye_pupil_anim_cb);
    lv_anim_delete(eye_state.blink_lid, eye_blink_anim_cb);
    if (eye_state.follow != NULL) {
        lv_timer_delete(eye_state.follow);
    }

    lv_obj_delete(eye_root);
    eye_root = NULL;
    memset(&eye_state, 0, sizeof(eye_state));
}
