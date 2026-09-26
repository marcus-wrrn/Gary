#include "gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <math.h>

#define KELVIN_OFFSET 273.15f

static const char* TAG = "GPIO";
static adc_oneshot_unit_handle_t adc1;
static adc_cali_handle_t thermistor_cali;

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

    res = adc_oneshot_config_channel(adc1, THERMISTOR_CHANNEL, &config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize thermistor ADC channel");
        return res;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = THERMISTOR_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    res = adc_cali_create_scheme_curve_fitting(&cali_cfg, &thermistor_cali);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize thermistor ADC calibration");
        return res;
    }

    return res;
}

esp_err_t gpio_adc_oneshot_read(adc_channel_t channel, int* level) {
    return adc_oneshot_read(adc1, channel, level);
}

esp_err_t gpio_read_thermistor(float* temp_c) {
    int raw = 0;
    esp_err_t res = adc_oneshot_read(adc1, THERMISTOR_CHANNEL, &raw);
    if (res != ESP_OK) {
        ESP_LOGI(TAG, "Unable to read thermistor value");
        return res;
    }

    int mv = 0;
    res = adc_cali_raw_to_voltage(thermistor_cali, raw, &mv);
    if (res != ESP_OK) {
        ESP_LOGI(TAG, "Unable to calibrate raw voltage");
        return res;
    }

    // A reading at either rail means the NTC is shorted or disconnected
    if (mv <= 0 || mv >= THERMISTOR_SUPPLY_MV) {
        ESP_LOGI(TAG, "Thermistor either shorted or disconnected");
        return ESP_ERR_INVALID_RESPONSE;
    }

    float ntc_ohms = THERMISTOR_SERIES_OHMS * mv / (float)(THERMISTOR_SUPPLY_MV - mv);

    // Beta equation: 1/T = 1/T0 + ln(R/R0)/B
    float inv_t = 1.0f / (THERMISTOR_NOMINAL_C + KELVIN_OFFSET)
                + logf(ntc_ohms / THERMISTOR_NOMINAL_OHMS) / THERMISTOR_BETA;
    *temp_c = 1.0f / inv_t - KELVIN_OFFSET;

    return ESP_OK;
}

esp_err_t gpio_init_digital() {
    uint64_t output_pins = (1ULL << SSD1306_RST_PIN)
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