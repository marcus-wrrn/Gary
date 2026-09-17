#include "gpio.h"
#include "esp_log.h"

static const char* TAG = "GPIO";

// Every conversion on the S3 is a 4 byte adc_digi_output_data_t (type2)
#define ADC_CONV_BYTES          SOC_ADC_DIGI_RESULT_BYTES
#define ADC_FRAME_CONVERSIONS   256
#define ADC_FRAME_SIZE          (ADC_FRAME_CONVERSIONS * ADC_CONV_BYTES)
#define ADC_POOL_SIZE           (ADC_FRAME_SIZE * 4)

#ifdef MICROPHONE_OUT_CHANNEL
// Pattern alternates mic/moisture so mic samples stay evenly spaced
static const adc_channel_t adc_channels[] = { MICROPHONE_OUT_CHANNEL, MOISTURE_SENSOR_CHANNEL };
#else
static const adc_channel_t adc_channels[] = { MOISTURE_SENSOR_CHANNEL };
#endif
#define ADC_PATTERN_LEN (sizeof(adc_channels) / sizeof(adc_channels[0]))

static adc_continuous_handle_t adc1;
static uint8_t adc_frame[ADC_FRAME_SIZE];
static int moisture_level = -1;

esp_err_t gpio_init_adc() {
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = ADC_POOL_SIZE,
        .conv_frame_size = ADC_FRAME_SIZE,
        // drop stale samples instead of stalling when the consumer falls behind
        .flags.flush_pool = 1,
    };

    esp_err_t res = adc_continuous_new_handle(&handle_cfg, &adc1);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC continuous handle");
        return res;
    }

    adc_digi_pattern_config_t pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    for (size_t i = 0; i < ADC_PATTERN_LEN; i++) {
        pattern[i].unit = ADC_UNIT_1;
        pattern[i].channel = adc_channels[i];
        pattern[i].atten = ADC_ATTEN_DB_12;
        pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }

    adc_continuous_config_t config = {
        .pattern_num = ADC_PATTERN_LEN,
        .adc_pattern = pattern,
        // sample rate is shared across the pattern, scale it so each channel gets the full rate
        .sample_freq_hz = MIC_SAMPLE_RATE_HZ * ADC_PATTERN_LEN,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    res = adc_continuous_config(adc1, &config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channels");
        return res;
    }

    res = adc_continuous_start(adc1);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC continuous conversion");
        return res;
    }

    return res;
}

// Pulls one frame from the ADC pool, routing mic samples into mic_out and caching moisture
static esp_err_t adc_read_frame(uint16_t* mic_out, size_t max_samples, size_t* out_samples, uint32_t timeout_ms) {
    uint32_t read_len = ADC_FRAME_SIZE;
    if (mic_out != NULL && max_samples * ADC_PATTERN_LEN * ADC_CONV_BYTES < read_len) {
        read_len = max_samples * ADC_PATTERN_LEN * ADC_CONV_BYTES;
    }

    uint32_t len = 0;
    esp_err_t res = adc_continuous_read(adc1, adc_frame, read_len, &len, timeout_ms);
    if (res != ESP_OK) {
        return res;
    }

    size_t count = 0;
    for (uint32_t i = 0; i + ADC_CONV_BYTES <= len; i += ADC_CONV_BYTES) {
        adc_digi_output_data_t* p = (adc_digi_output_data_t*)&adc_frame[i];
        adc_channel_t channel = p->type2.channel;

#ifdef MICROPHONE_OUT_CHANNEL
        if (channel == MICROPHONE_OUT_CHANNEL && mic_out != NULL && count < max_samples) {
            mic_out[count++] = p->type2.data;
            continue;
        }
#endif
        if (channel == MOISTURE_SENSOR_CHANNEL) {
            moisture_level = p->type2.data;
        }
    }

    if (out_samples != NULL) {
        *out_samples = count;
    }
    return ESP_OK;
}

#ifdef MICROPHONE_OUT_CHANNEL
esp_err_t gpio_adc_read_mic(uint16_t* samples, size_t max_samples, size_t* out_samples, uint32_t timeout_ms) {
    if (samples == NULL || out_samples == NULL || max_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return adc_read_frame(samples, max_samples, out_samples, timeout_ms);
}
#endif

esp_err_t gpio_adc_read_moisture(int* level) {
#ifndef MICROPHONE_OUT_CHANNEL
    // Without a mic consumer draining the pool, moisture has to drain it itself
    adc_read_frame(NULL, 0, NULL, 0);
#endif
    if (moisture_level < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    *level = moisture_level;
    return ESP_OK;
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
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO Input pins");
        return err;
    }

    return ESP_OK;
}
