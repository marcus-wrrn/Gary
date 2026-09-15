#include "display_task.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "eye.h"

#define DISPLAY_TASK_STACK_SIZE 8192
#define DISPLAY_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)
#define DISPLAY_TASK_CORE       1
#define DISPLAY_QUEUE_LENGTH    8
// Upper bound on sleeping between LVGL updates, since lv_timer_handler() returns LV_NO_TIMER_READY when idle
#define DISPLAY_MAX_WAIT_MS     20

static const char* TAG = "DISPLAY_TASK";

QueueHandle_t display_queue = NULL;

static void display_handle_message(const struct display_task_data_t* msg) {
    display_set_eye_animation(msg->play_animation);
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
        // With a 100 Hz tick short waits round to 0 ticks, which would spin and starve lower-priority tasks
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
    err = display_draw_eye();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw eye");
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
