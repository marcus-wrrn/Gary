#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gpio.h"
#include "i2c.h"
#include "ssd1306.h"
#include "plant_state.h"

static const char* TAG = "INIT";
static char disp_buffer[128];

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

    err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2c");
        return;
    }

    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    int moisture_level = 0;
    PlantState state = plant_get_current_state();
    while (1) {
        // decide moves based on current state

        // gather information

        // update display, sound and other tasks
        


        err = gpio_adc_oneshot_read(MOISTURE_SENSOR_CHANNEL, &moisture_level);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Invalid read of moisture sensor");
        }
        

        int len = snprintf(disp_buffer, sizeof(disp_buffer), "Moisture Level: %d", moisture_level);
        clear_display();
        display_draw_string(0, 0, disp_buffer, len);
        display_draw();
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}