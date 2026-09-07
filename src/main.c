#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gpio.h"
#include "i2c.h"
#include "ssd1306.h"

static const char* TAG = "INIT";

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

    // initalize Vext
    err = gpio_set_level(VEXT_PIN, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to turn on external voltage pin");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    char str[] = "Hello World\nMeep?";
    display_draw_string(0, 0, str, strlen(str));

    err = display_draw();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw to display");
        return;
    }

    // int moisture_level = 0;
    // while (1) {
    //     err = gpio_adc_oneshot_read(MOISTURE_SENSOR_CHANNEL, &moisture_level);
    //     if (err != ESP_OK) {
    //         ESP_LOGE(TAG, "Invalid read of moisture sensor");
    //     } else {
    //         ESP_LOGI(TAG, "Moisture Level: %d", moisture_level);
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
    
}