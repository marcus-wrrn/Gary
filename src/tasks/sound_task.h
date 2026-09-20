#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

typedef enum Track {
    MEEP = 0
} Track;

typedef struct sound_task_data_t {
    Track track;
} sound_task_data_t;


// queue for passing information to the speakers
extern QueueHandle_t sound_queue;

// initializes FreeRTOS task for playing sound on the speakers/listening to noises
esp_err_t start_sound_task();
