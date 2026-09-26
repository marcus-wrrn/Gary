#include "gpio.h"
#include "esp_log.h"

static const char* TAG = "GPIO";
static adc_oneshot_unit_handle_t adc1;

esp_err_t gpio_init_adc() {
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t res = adc_oneshot_new_unit(&init_cfg, &adc1);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize new ADC oneshot unit");
        return res;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
    };
    res = adc_oneshot_config_channel(adc1, MOISTURE_SENSOR_CHANNEL, &config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC channel");
        return res;
    }

    return res;
}

esp_err_t gpio_adc_oneshot_read(adc_channel_t channel, int* level) {
    return adc_oneshot_read(adc1, channel, level);
}

esp_err_t gpio_init_digital() {
    uint64_t output_pins = (1ULL << SSD1306_RST_PIN);
#ifdef HELTEC_BOARD
    // Vext only exists on the Heltec board
    output_pins |= (1ULL << VEXT_PIN);
#else
    output_pins |= (1ULL << SPI_DISPLAY_DC_PIN) | (1ULL << DISPLAY_RST_PIN);
#endif

    gpio_config_t io_conf = {
        .pin_bit_mask = output_pins,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO Output pins");
        return err;
    }

    io_conf.pin_bit_mask = (1ULL << TEST_BUTTON_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO Input pins");
        return err;
    }

    return ESP_OK;
}