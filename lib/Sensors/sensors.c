#include "sensors.h"
#include <math.h>
#include "esp_log.h"
#include "gpio.h"
#include "sht31.h"

#define KELVIN_OFFSET 273.15f

static const char* TAG = "SENSORS";
static adc_cali_handle_t thermistor_cali;

esp_err_t sensors_init() {
    esp_err_t res = gpio_adc_config_channel(MOISTURE_SENSOR_CHANNEL);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize moisture sensor");
        return res;
    }

    res = gpio_adc_config_channel(LIGHT_SENSOR_CHANNEL);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize light sensor");
        return res;
    }

    res = gpio_adc_config_channel(THERMISTOR_CHANNEL);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize thermistor");
        return res;
    }

    res = gpio_adc_create_cali(THERMISTOR_CHANNEL, &thermistor_cali);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize thermistor calibration");
        return res;
    }

    res = sht31_init();
    if (res != ESP_OK) {
        ESP_LOGW(TAG, "SHT31 unavailable: %s", esp_err_to_name(res));
    }

    return ESP_OK;
}

esp_err_t sensors_read_moisture(int* level) {
    return gpio_adc_oneshot_read(MOISTURE_SENSOR_CHANNEL, level);
}

esp_err_t sensors_read_light(int* level) {
    return gpio_adc_oneshot_read(LIGHT_SENSOR_CHANNEL, level);
}

esp_err_t sensors_read_thermistor(float* temp_c) {
    int raw = 0;
    esp_err_t res = gpio_adc_oneshot_read(THERMISTOR_CHANNEL, &raw);
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

esp_err_t sensors_read_sht31(float* temp_c, float* humidity_pct) {
    return sht31_read(temp_c, humidity_pct);
}
