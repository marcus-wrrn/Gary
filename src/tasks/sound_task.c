#include "sound_task.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2s.h"
#include "audio.h"

#define SOUND_TASK_STACK_SIZE   8192
#define SOUND_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)
#define SOUND_TASK_CORE         0
#define SOUND_QUEUE_LENGTH      8

static const char* TAG = "SOUND_TASK";
QueueHandle_t sound_queue = NULL;

static void sound_handle_message(const struct sound_task_data_t* msg) {
    ESP_LOGI(TAG, "Playing Track: %d", msg->track);

    switch (msg->track)
    {
    case (enum Track)MEEP:
        play_track("/plant/meep.wav");
        break;
    default:
        break;
    }
}

static void sound_task(void* arg) {
    (void)arg;
    struct sound_task_data_t msg;

    while (1) {
        if (xQueueReceive(sound_queue, &msg, 100) == pdTRUE) {
            sound_handle_message(&msg);
        }
    }
}

esp_err_t start_sound_task() {
    if (sound_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S");
        return ret;
    }

    sound_queue = xQueueCreate(SOUND_QUEUE_LENGTH, sizeof(struct sound_task_data_t));
    if (sound_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create sound queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(sound_task, "sound", SOUND_TASK_STACK_SIZE, NULL, 
        SOUND_TASK_PRIORITY, NULL, SOUND_TASK_CORE);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sound task");
        vQueueDelete(sound_queue);
        sound_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}