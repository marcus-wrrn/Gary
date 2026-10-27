#include "i2c.h"
#include "esp_log.h"

static const char* TAG = "I2C";

static i2c_master_bus_handle_t bus_handle;

esp_err_t i2c_init() {
    i2c_master_bus_config_t i2c_mst_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = I2C_DISPLAY_SCL_PIN,
        .sda_io_num = I2C_DISPLAY_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t res = i2c_new_master_bus(&i2c_mst_cfg, &bus_handle);

    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize new master bus");
    }
    return res;
}

esp_err_t i2c_add_device_to_bus(i2c_master_dev_handle_t* dev_handle, uint16_t device_addr) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = 100000,
    };

    esp_err_t res = i2c_master_bus_add_device(bus_handle, &dev_cfg, dev_handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Unable to add device to bus");
    }
    return res;
}

esp_err_t i2c_probe(uint16_t device_addr) {
    return i2c_master_probe(bus_handle, device_addr, 50);
}

void i2c_probe_addresses() {
    for (uint16_t i = 0x00; i <= 0xFF; i += 1) {
        esp_err_t res = i2c_master_probe(bus_handle, i, -1);
        if (res == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at address: 0x%x", (uint8_t)i & 0xFF);
        }
    }
}