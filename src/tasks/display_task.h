#pragma once
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

extern QueueHandle_t display_queue;

esp_err_t start_display_task();