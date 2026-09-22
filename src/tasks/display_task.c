#include "display_task.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "eye.h"
#include "sound_sphere.h"

#define DISPLAY_TASK_STACK_SIZE 8192
#define DISPLAY_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)
#define DISPLAY_TASK_CORE       1
#define DISPLAY_QUEUE_LENGTH    8
// Upper bound on sleeping between LVGL updates, since lv_timer_handler() returns LV_NO_TIMER_READY when idle
#define DISPLAY_MAX_WAIT_MS     20

static const char* TAG = "DISPLAY_TASK";

static DisplayState state = EYE;
QueueHandle_t display_queue = NULL;

static esp_err_t display_draw(DisplayState target) {
    switch (target) {
    case EYE:
        return display_draw_eye();
    case AUDIO_VIS:
        return display_draw_sound_sphere();
    }
    return ESP_ERR_INVALID_ARG;
}

static void display_clear(DisplayState target) {
    switch (target) {
    case EYE:
        display_clear_eye();
        break;
    case AUDIO_VIS:
        display_clear_sound_sphere();
        break;
    }
}

static void display_set_animation(bool play) {
    switch (state) {
    case EYE:
        display_set_eye_animation(play);
        break;
    case AUDIO_VIS:
        display_set_sound_sphere_animation(play);
        break;
    }
}

static void display_change_state(DisplayState target) {
    if (target == state) {
        return;
    }

    display_clear(state);

    esp_err_t err = display_draw(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw state %d: %s", target, esp_err_to_name(err));
        // The old state is already off the screen, and a blank display is worse
        // than the one the caller was trying to leave.
        if (display_draw(state) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore state %d, display is blank", state);
        }
        return;
    }

    state = target;
    ESP_LOGI(TAG, "Display state: %d", state);
}

static void display_handle_message(const struct display_task_data_t* msg) {
    display_change_state(msg->state);
    display_set_animation(msg->play_animation);
}

// Owns LVGL: no other task may call lv_* functions, they send a display_task_data_t instead
static void display_task(void* arg) {
    LV_UNUSED(arg);
    struct display_task_data_t msg;

    while (1) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms > DISPLAY_MAX_WAIT_MS) {
            wait_ms = DISPLAY_MAX_WAIT_MS;
        }
        // A 0 ms wait rounds to 0 ticks, which would spin and starve lower-priority tasks
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }

        // Sleeping on the queue instead of vTaskDelay() lets a message wake the task straight away
        if (xQueueReceive(display_queue, &msg, wait_ticks) == pdTRUE) {
            do {
                display_handle_message(&msg);
            } while (xQueueReceive(display_queue, &msg, 0) == pdTRUE);
        }
    }
}

esp_err_t start_display_task() {
    if (display_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Safe to touch LVGL here because the display task isn't running yet
    esp_err_t err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return err;
    }
    err = display_draw(state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw state %d: %s", state, esp_err_to_name(err));
        return err;
    }

    display_queue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(struct display_task_data_t));
    if (display_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create display queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(display_task, "display", DISPLAY_TASK_STACK_SIZE, NULL,
                                                 DISPLAY_TASK_PRIORITY, NULL, DISPLAY_TASK_CORE);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        vQueueDelete(display_queue);
        display_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
