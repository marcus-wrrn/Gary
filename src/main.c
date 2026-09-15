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
#include "eye.h"

#define DEBOUNCE_DELAY_US 200000ULL

static const char* TAG = "INIT";
static char disp_buffer[128];

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

void app_main() {
#if !DEVELOPMENT
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    esp_err_t err = gpio_init_digital();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO pins");
        return;
    }

    err = gpio_init_adc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC closing program");
        return;
    }

    button_queue = xQueueCreate(10, sizeof(bool));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(TEST_BUTTON_PIN, button_isr, NULL);
    bool button_flag;

    // err = i2c_init();
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize I2C");
    //     return;
    // }

    err = i2s_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S");
        return;
    }

    err = sd_card_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return;
    }

    err = spi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI");
        return;
    }

    err = start_display_task();

    ESP_LOGI(TAG, "Playing startup tone");
    err = i2s_play_beep(440, 500);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play startup tone: %s", esp_err_to_name(err));
    }

    int moisture_level = 0;
    int ticks_since_beep = 0;
    PlantState state = plant_get_current_state();
    bool play_animation = true;

    while (1) {
        if (xQueueReceive(button_queue, &button_flag, 0)) {
            //err = i2s_play_beep(440, 500);
            // if (err != ESP_OK) {
            //     ESP_LOGE(TAG, "Failed to play tone: %s", esp_err_to_name(err));
            // }
            play_animation = !play_animation;
            display_task_data_t anim = {
                .play_animation = play_animation
            };
            xQueueSendToBack(display_queue, &anim, 0);
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