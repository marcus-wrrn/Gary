#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gpio.h"
// #include "i2c.h"
#include "i2s.h"
// #include "ssd1306.h"
#include "plant_state.h"
#include "sd_card.h"
#include "spi.h"
#include "tasks/display_task.h"
#include "tasks/sound_task.h"
#include "eye.h"

#define DEBOUNCE_DELAY_US 200000ULL

static const char* TAG = "INIT";

static volatile uint64_t last_isr_time = 0;
static QueueHandle_t button_queue;

static void IRAM_ATTR button_isr(void *arg) {
    uint64_t now = esp_timer_get_time();
    if (now - last_isr_time > DEBOUNCE_DELAY_US) {
        bool flag = true;
        BaseType_t higher_priority_task_woken = pdFALSE;
        xQueueSendFromISR(button_queue, &flag, &higher_priority_task_woken);
        last_isr_time = now;
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static esp_err_t setup_peripherals() {
    esp_err_t ret = gpio_init_digital();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO pins");
        return ret;
    }

    ret = gpio_init_adc();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC closing program");
        return ret;
    }

    // ret = i2c_init();
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize I2C");
    //     return ret;
    // }

    ret = sd_card_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return ret;
    }

    ret = spi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI");
        return ret;
    }

    return ret;
}

void app_main() {
#if !DEVELOPMENT
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    esp_err_t err = setup_peripherals();
    if (err != ESP_OK) {
        return;
    }

    button_queue = xQueueCreate(10, sizeof(bool));
    if (button_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button queue");
        return;
    }
    gpio_install_isr_service(0);
    gpio_isr_handler_add(TEST_BUTTON_PIN, button_isr, NULL);
    bool button_flag;

    err = start_display_task();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start display task: %s", esp_err_to_name(err));
        return;
    }

    err = start_sound_task();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sound task: %s", esp_err_to_name(err));
        return;
    }

    int moisture_level = 0;
    PlantState state = plant_get_current_state();
    (void)state;
    bool play_animation = true;

    while (1) {
        if (xQueueReceive(button_queue, &button_flag, 0)) {
            play_animation = !play_animation;
            display_task_data_t anim = {
                .play_animation = play_animation
            };
            xQueueSendToBack(display_queue, &anim, 0);

            sound_task_data_t sound = {
                .track = MEEP
            };
            xQueueSendToBack(sound_queue, &sound, 0);
            ESP_LOGI(TAG, "Button pressed");
        }

        // decide moves based on current state

        // gather information

        // update display, sound and other tasks


        err = gpio_adc_oneshot_read(MOISTURE_SENSOR_CHANNEL, &moisture_level);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Invalid read of moisture sensor");
        }
        ESP_LOGI(TAG, "Moisture Level: %d", moisture_level);

        // int len = snprintf(disp_buffer, sizeof(disp_buffer), "Moisture Level: %d", moisture_level);
        // clear_display();
        // display_draw_string(0, 0, disp_buffer, len);
        // display_draw();
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}