#include "sht31.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "i2c.h"

#define SHT31_CMD_MEASURE_HIGH  0x2400  // single shot, high repeatability, no clock stretching
#define SHT31_CMD_SOFT_RESET    0x30A2
#define SHT31_MEASURE_MS        16      // datasheet max is 15.5 ms at high repeatability
#define SHT31_RESET_MS          2       // datasheet max is 1.5 ms
#define SHT31_I2C_TIMEOUT_MS    50
#define SHT31_CRC_POLY          0x31
#define SHT31_CRC_INIT          0xFF

static const char* TAG = "SHT31";

static i2c_master_dev_handle_t sht31_dev;
static bool sht31_ready = false;

static esp_err_t sht31_send_command(uint16_t cmd) {
    uint8_t buf[2] = { cmd >> 8, cmd & 0xFF };
    return i2c_master_transmit(sht31_dev, buf, sizeof(buf), SHT31_I2C_TIMEOUT_MS);
}

// CRC-8 over each 16-bit word, as the datasheet specifies
static uint8_t sht31_crc(const uint8_t* data, size_t len) {
    uint8_t crc = SHT31_CRC_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (crc << 1) ^ SHT31_CRC_POLY : crc << 1;
        }
    }
    return crc;
}

esp_err_t sht31_reset() {
    gpio_set_level(SHT31_RST_PIN, 0);
    esp_rom_delay_us(10);
    gpio_set_level(SHT31_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(SHT31_RESET_MS));
    return ESP_OK;
}

esp_err_t sht31_init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SHT31_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t res = gpio_config(&io_conf);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize reset pin");
        return res;
    }

    io_conf.pin_bit_mask = (1ULL << SHT31_ALERT_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    res = gpio_config(&io_conf);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize alert pin");
        return res;
    }

    sht31_reset();

    uint16_t addr = SHT31_I2C_ADDR;
    if (i2c_probe(addr) != ESP_OK) {
        addr = SHT31_I2C_ADDR_ALT;
        if (i2c_probe(addr) != ESP_OK) {
            ESP_LOGE(TAG, "No sensor at 0x%x or 0x%x", SHT31_I2C_ADDR, SHT31_I2C_ADDR_ALT);
            return ESP_ERR_NOT_FOUND;
        }
    }

    res = i2c_add_device_to_bus(&sht31_dev, addr);
    if (res != ESP_OK) {
        return res;
    }

    res = sht31_send_command(SHT31_CMD_SOFT_RESET);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed");
        return res;
    }
    vTaskDelay(pdMS_TO_TICKS(SHT31_RESET_MS));

    sht31_ready = true;
    ESP_LOGI(TAG, "Found at 0x%x", addr);
    return ESP_OK;
}

esp_err_t sht31_read(float* temp_c, float* humidity_pct) {
    if (!sht31_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t res = sht31_send_command(SHT31_CMD_MEASURE_HIGH);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start measurement");
        return res;
    }
    vTaskDelay(pdMS_TO_TICKS(SHT31_MEASURE_MS));

    // Temperature MSB, LSB, CRC then humidity MSB, LSB, CRC
    uint8_t buf[6];
    res = i2c_master_receive(sht31_dev, buf, sizeof(buf), SHT31_I2C_TIMEOUT_MS);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement");
        return res;
    }

    if (sht31_crc(&buf[0], 2) != buf[2] || sht31_crc(&buf[3], 2) != buf[5]) {
        ESP_LOGE(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_t = (buf[0] << 8) | buf[1];
    uint16_t raw_rh = (buf[3] << 8) | buf[4];

    if (temp_c) {
        *temp_c = -45.0f + 175.0f * raw_t / 65535.0f;
    }
    if (humidity_pct) {
        *humidity_pct = 100.0f * raw_rh / 65535.0f;
    }
    return ESP_OK;
}

bool sht31_alert_active() {
    return gpio_get_level(SHT31_ALERT_PIN) == 1;
}
