#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

typedef struct display_task_data_t {
    bool play_animation;
} display_task_data_t;

// queue for passing information to the display
extern QueueHandle_t display_queue;

// initializes and starts FreeRTOS task for displaying content to the screen
esp_err_t start_display_task();
