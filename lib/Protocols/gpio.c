#include "gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_cali_scheme.h"

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
    }
    return res;
}

esp_err_t gpio_adc_config_channel(adc_channel_t channel) {
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
    };
    esp_err_t res = adc_oneshot_config_channel(adc1, channel, &config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC channel %d", channel);
    }
    return res;
}

esp_err_t gpio_adc_create_cali(adc_channel_t channel, adc_cali_handle_t* cali) {
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t res = adc_cali_create_scheme_curve_fitting(&cali_cfg, cali);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC calibration for channel %d", channel);
    }
    return res;
}

esp_err_t gpio_adc_oneshot_read(adc_channel_t channel, int* level) {
    return adc_oneshot_read(adc1, channel, level);
}

esp_err_t gpio_init_digital() {
    uint64_t output_pins = (1ULL << DISPLAY_BL_PIN)
                         | (1ULL << SPI_DISPLAY_DC_PIN)
                         | (1ULL << DISPLAY_RST_PIN);

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
    // Keep the backlight dark until display_init() has reset the panel and drawn a frame
    gpio_set_level(DISPLAY_BL_PIN, 0);

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