#include "spi.h"
#include "esp_log.h"
#include "driver/spi_master.h"

static const char* TAG = "SPI";

spi_device_handle_t display_handle;

// Runs right before each queued transaction is clocked out, so the D/C level
// is set for the transaction that's actually about to go on the wire (not
// whichever one was in flight when spi_write_cmd/spi_write_data was called).
// t->user carries the level to apply: 0 = command, 1 = data.
static void spi_pre_transfer_callback(spi_transaction_t* t) {
    int dc_level = (int)(intptr_t)t->user;
    gpio_set_level(SPI_DISPLAY_DC_PIN, dc_level);
}

esp_err_t spi_init() {
    spi_bus_config_t bus_config = {
        .miso_io_num = SPI_MISO_PIN,
        .mosi_io_num = SPI_MOSI_PIN,
        .sclk_io_num = SPI_SCK_PIN,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1,
        .max_transfer_sz = 48 * 240 * 2
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return err;
    }

    spi_device_interface_config_t dev_config = {
        // Above 26 MHz only works because these are SPI2's IOMUX pins; routing
        // through the GPIO matrix instead would need this dropped back down.
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = SPI_CS_PIN,
        .queue_size = 7,
        .pre_cb = spi_pre_transfer_callback,
    };

    err = spi_bus_add_device(SPI2_HOST, &dev_config, &display_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add display device to SPI bus");
        return err;
    }

    return ESP_OK;
}

esp_err_t spi_write_cmd(uint8_t cmd) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = (void*)0,
    };
    return spi_device_polling_transmit(display_handle, &t);
}

esp_err_t spi_write_data(const uint8_t* data, size_t len) {
    if (len == 0) {
        return ESP_OK;
    }
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void*)1,
    };
    return spi_device_polling_transmit(display_handle, &t);
}