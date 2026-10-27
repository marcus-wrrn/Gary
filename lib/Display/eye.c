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
#define EYE_PUPIL_GAP     EYE_STROKE  // kept between the pupil and the lid lines, clearing their stroke
#define EYE_LASH_COUNT    10
#define EYE_LASH_GAP      6
#define EYE_LASH_LEN      10

// Pupil path: swings EYE_LOOK_X either side of centre, dipping EYE_LOOK_SAG px lower in the middle
#define EYE_LOOK_X        50
#define EYE_LOOK_SAG      8
#define EYE_LOOK_MS       1500
#define EYE_LOOK_PAUSE_MS 400

#define EYE_BLINK_DOWN_MS  500
#define EYE_BLINK_HOLD_MS  560
#define EYE_BLINK_UP_MS    500
#define EYE_BLINK_PAUSE_MS 1500  // minimum pupil travel time (legs plus pauses) between blinks


#define EYE_FOLLOW_TICK_MS      33   
#define EYE_FOLLOW_OMEGA        11.0f  // spring stiffness in rad/s; settles in about 4 / this seconds
#define EYE_FOLLOW_DEADBAND     0.2f   // distance the gaze has to move before the eye glances again
#define EYE_FOLLOW_DWELL_MS     500    // minimum time between glances
// How long a reading has to hold before the eye eases back towards centre in the same direction.
// The reading dips for a moment when echoes briefly win the correlation, and without this the
// eye would follow each dip and snap straight back. Glances outwards or across stay immediate.
#define EYE_FOLLOW_INWARD_MS    600
#define EYE_FOLLOW_LINGER_MS    1200   // how long the eye keeps looking after the sound goes
// The lids leave the pupil about a quarter of its sideways travel up and down, and a talker is
// rarely far above or below the mics, so the vertical reading is scaled up to make it visible
#define EYE_FOLLOW_GAIN_Y       2.5f

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
    // position of the eye
    float pupil_x;
    float pupil_y;
    float lid_h;  
    lv_timer_t* follow;
    float gaze_x;       
    float gaze_y;
    float gaze_vel_x;    
    float gaze_vel_y;
    // where the eye is moving to
    float target_x;      
    float target_y;
    uint32_t glance_ms;
    uint32_t inward_ms;  
    uint32_t heard_ms;   
    uint32_t step_ms;    
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

// dx and dy are the pupil centre's offset from the middle of the eye, in pixels
static void eye_place_pupil(float dx, float dy) {
    eye_state.pupil_x = DISPLAY_H_RES / 2.0f + dx;
    eye_state.pupil_y = DISPLAY_V_RES / 2.0f + dy;
    eye_update_pupil();
}

// v runs from -1000 (left end of the path) to 1000 (right end)
static void eye_pupil_anim_cb(void* var, int32_t v) {
    LV_UNUSED(var);
    float t = v / 1000.0f;
    eye_place_pupil(EYE_LOOK_X * t, EYE_LOOK_SAG * (1 - t * t));
}

static float eye_pupil_reach(float dx, float dy) {
    const float heights[] = {EYE_CREASE_H, -EYE_LOWER_LID_H};
    float reach = INFINITY;
    for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
        float h = fabsf(heights[i]);
        float r = (EYE_HALF_WIDTH * EYE_HALF_WIDTH + h * h) / (2 * h);
        float centre_y = heights[i] > 0 ? r - h : -(r - h);
        float room = r - EYE_PUPIL_R - EYE_PUPIL_GAP;
        // Where the ray from the middle leaves the shrunken circle: |t * d - c| = room
        float along = dy * centre_y;
        float t = along + sqrtf(along * along - centre_y * centre_y + room * room);
        if (t < reach) reach = t;
    }
    return reach;
}

// Pixel offset of the pupil for a gaze in the unit disc
static void eye_gaze_offset(float gx, float gy, float* dx, float* dy) {
    float len = hypotf(gx, gy);
    if (len < 1e-3f) {
        *dx = 0.0f;
        *dy = 0.0f;
        return;
    }
    float reach = eye_pupil_reach(gx / len, gy / len);
    *dx = gx * reach;
    *dy = gy * reach;
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

static void eye_follow_cb(lv_timer_t* timer) {
    LV_UNUSED(timer);
    uint32_t now = lv_tick_get();
    float dt = lv_tick_diff(now, eye_state.step_ms) / 1000.0f;
    eye_state.step_ms = now;
    // A late tick, e.g. after the animation was paused, would otherwise fling the pupil
    if (dt > 0.1f) dt = 0.1f;

    float want_x = 0.0f;
    float want_y = 0.0f;
    float heard_x, heard_y;
    if (direction_get_position(&heard_x, &heard_y)) {
        // Taken as is: on the device, negating y (to turn up into screen-down) looked the wrong way
        want_x = heard_x;
        want_y = heard_y * EYE_FOLLOW_GAIN_Y;
        float heard_len = hypotf(want_x, want_y);
        if (heard_len > 1.0f) {
            want_x /= heard_len;
            want_y /= heard_len;
        }
        eye_state.heard_ms = now;
    } else if (lv_tick_diff(now, eye_state.heard_ms) < EYE_FOLLOW_LINGER_MS) {
        want_x = eye_state.target_x;
        want_y = eye_state.target_y;
    }

    // Returning to centre once the sound has gone isn't held back, as the linger already waits
    bool far = hypotf(want_x - eye_state.target_x, want_y - eye_state.target_y) >= EYE_FOLLOW_DEADBAND;
    bool inward = want_x * eye_state.target_x + want_y * eye_state.target_y > 0.0f &&
                  hypotf(want_x, want_y) < hypotf(eye_state.target_x, eye_state.target_y);
    if (!far || !inward) {
        eye_state.inward_ms = now;
    }
    bool held = !inward || lv_tick_diff(now, eye_state.inward_ms) >= EYE_FOLLOW_INWARD_MS;

    if (far && held && lv_tick_diff(now, eye_state.glance_ms) >= EYE_FOLLOW_DWELL_MS) {
        eye_state.target_x = want_x;
        eye_state.target_y = want_y;
        eye_state.glance_ms = now;
        eye_state.inward_ms = now;
    }

    // Semi-implicit Euler, stable while OMEGA * dt stays well under 1
    const float w = EYE_FOLLOW_OMEGA;
    float accel_x = w * w * (eye_state.target_x - eye_state.gaze_x) - 2.0f * w * eye_state.gaze_vel_x;
    float accel_y = w * w * (eye_state.target_y - eye_state.gaze_y) - 2.0f * w * eye_state.gaze_vel_y;
    eye_state.gaze_vel_x += accel_x * dt;
    eye_state.gaze_vel_y += accel_y * dt;
    eye_state.gaze_x += eye_state.gaze_vel_x * dt;
    eye_state.gaze_y += eye_state.gaze_vel_y * dt;
    float len = hypotf(eye_state.gaze_x, eye_state.gaze_y);
    if (len > 1.0f) {
        eye_state.gaze_x /= len;
        eye_state.gaze_y /= len;
    }

    // The spring creeps by fractions of a pixel long after it has settled, and moving the clip box
    // redraws it, so the pupil is only placed when it would land on a different pixel
    float dx, dy;
    eye_gaze_offset(eye_state.gaze_x, eye_state.gaze_y, &dx, &dy);
    float x = DISPLAY_H_RES / 2.0f + dx;
    float y = DISPLAY_V_RES / 2.0f + dy;
    if (lroundf(x) != lroundf(eye_state.pupil_x) || lroundf(y) != lroundf(eye_state.pupil_y)) {
        eye_place_pupil(dx, dy);
    }
}

static esp_err_t eye_start_follow() {
    uint32_t now = lv_tick_get();
    eye_state.gaze_x = 0.0f;
    eye_state.gaze_y = 0.0f;
    eye_state.gaze_vel_x = 0.0f;
    eye_state.gaze_vel_y = 0.0f;
    eye_state.target_x = 0.0f;
    eye_state.target_y = 0.0f;
    eye_state.glance_ms = now;
    eye_state.inward_ms = now;
    eye_state.heard_ms = now - EYE_FOLLOW_LINGER_MS;
    eye_state.step_ms = now;
    eye_place_pupil(0.0f, 0.0f);

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
