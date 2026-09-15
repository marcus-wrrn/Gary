#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi.h"
#include "src/draw/sw/lv_draw_sw_utils.h"

// Must stay within max_transfer_sz in spi_init(), since each flush is sent as one transaction
#define DISPLAY_DRAW_BUF_SIZE (DISPLAY_H_RES * 48 * 2)

static const char* TAG = "DISPLAY";
static lv_display_t* display = NULL;

static uint32_t display_tick_cb(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void display_send_cmd(lv_display_t* disp, const uint8_t* cmd, size_t cmd_size, const uint8_t* param, size_t param_size) {
    spi_write_cmd(cmd[0]);
    spi_write_data(param, param_size);
}

static void display_send_color(lv_display_t* disp, const uint8_t* cmd, size_t cmd_size, uint8_t* param, size_t param_size) {
    spi_write_cmd(cmd[0]);
    // LVGL renders RGB565 little endian, the ILI9341 expects big endian
    lv_draw_sw_rgb565_swap(param, param_size / 2);
    spi_write_data(param, param_size);
    lv_display_flush_ready(disp);
}

static void display_hw_reset() {
    gpio_set_level(DISPLAY_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DISPLAY_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

esp_err_t display_init() {
    display_hw_reset();

    lv_init();
    lv_tick_set_cb(display_tick_cb);

    display = lv_ili9341_create(DISPLAY_H_RES, DISPLAY_V_RES, LV_LCD_FLAG_NONE, display_send_cmd, display_send_color);
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to create ILI9341 display");
        return ESP_FAIL;
    }
    // This module scans with rows and columns transposed and RGB order compared to
    // LVGL's ILI9341 defaults, so set MADCTL's swap bit and leave BGR off.
    lv_lcd_generic_mipi_set_address_mode(display, false, DISPLAY_FLIP_VERTICAL, true, false);

    uint8_t* draw_buf = heap_caps_malloc(DISPLAY_DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    if (draw_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate draw buffer");
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(display, draw_buf, NULL, DISPLAY_DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    return ESP_OK;
}

esp_err_t display_show_image(const char* path) {
    if (display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_image_header_t header;
    if (lv_image_decoder_get_info(path, &header) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Unable to decode image: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    lv_obj_t* screen = lv_screen_active();
    //lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
    lv_obj_t* img = lv_image_create(screen);
    lv_image_set_src(img, path);
    lv_obj_center(img);
    return ESP_OK;
}

static void display_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    lv_obj_t* rect = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(rect);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_style_bg_color(rect, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
}

esp_err_t display_show_test_pattern() {
    if (display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    display_draw_rect(0, 0, 40, 120, 0xFF0000);
    display_draw_rect(DISPLAY_H_RES - 120, DISPLAY_V_RES - 40, 120, 40, 0x00FF00);
    return ESP_OK;
}
