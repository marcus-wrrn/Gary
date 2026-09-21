#include "i2s.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
static const char* TAG = "I2S";


#define BUFFER_LEN 64
i2s_chan_handle_t tx_handle;
i2s_chan_handle_t rx_handle;

esp_err_t i2s_init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize I2S channels");
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize TX channel config");
        return ret;
    }

    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize RX channel config");
        return ret;
    }

    return ret;
}

esp_err_t i2s_enable_tx() {
    esp_err_t ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to enable TX channel");
    }
    return ret;
}

esp_err_t i2s_disable_tx() {
    esp_err_t ret = i2s_channel_disable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to disable TX channel");
    }
    return ret;
}

esp_err_t i2s_enable_rx() {
    esp_err_t ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to enable RX channel");
    }
    return ret;
}

esp_err_t i2s_disable_rx() {
    esp_err_t ret = i2s_channel_disable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to disable RX channel");
    }
    return ret;
}

esp_err_t i2s_configure_stream(uint32_t sample_rate, uint16_t bit_depth, uint16_t num_channels) {
    if (num_channels != 1 && num_channels != 2) {
        ESP_LOGE(TAG, "Unsupported channel count: %u", (unsigned)num_channels);
        return ESP_ERR_INVALID_ARG;
    }
    if (bit_depth != 16 && bit_depth != 32) {
        ESP_LOGE(TAG, "Unsupported bit depth: %u", (unsigned)bit_depth);
        return ESP_ERR_INVALID_ARG;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    i2s_slot_mode_t slot_mode = (num_channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bit_depth, slot_mode);

    i2s_chan_handle_t handles[] = { tx_handle, rx_handle };
    for (int i = 0; i < 2; i++) {
        esp_err_t ret = i2s_channel_reconfig_std_clock(handles[i], &clk_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to set sample rate to %u", (unsigned)sample_rate);
            return ret;
        }

        ret = i2s_channel_reconfig_std_slot(handles[i], &slot_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to set slot format");
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t i2s_read(void* data, size_t len, size_t* bytes_read) {
    if (data == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2s_channel_read(rx_handle, data, len, bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read samples");
    }
    return ret;
}

esp_err_t i2s_write(const void* data, size_t len, size_t* bytes_written) {
    if (data == NULL || bytes_written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2s_channel_write(tx_handle, data, len, bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to write samples");
    }
    return ret;
}

esp_err_t i2s_play_beep(uint32_t freq_hz, uint32_t duration_ms) {
    esp_err_t err = i2s_configure_stream(I2S_TONE_SAMPLE_RATE, I2S_TONE_BIT_DEPTH, 2);
    if (err != ESP_OK) return err;

    err = i2s_enable_tx();
    if (err != ESP_OK) return err;

    err = i2s_play_tone(freq_hz, duration_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to play tone");
    }

    esp_err_t disable_err = i2s_disable_tx();
    return err != ESP_OK ? err : disable_err;
}

esp_err_t i2s_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    const uint32_t sample_rate = I2S_TONE_SAMPLE_RATE;
    const int32_t amplitude = 0x10000000;
    static int32_t tone_buf[BUFFER_LEN * 2];
    size_t bytes_written;

    // half_period is how many samples each half of the square wave lasts, so a
    // full cycle is twice that. Dividing by freq_hz alone would emit freq_hz / 2.
    uint32_t half_period = sample_rate / (2 * freq_hz);
    if (half_period == 0) return ESP_ERR_INVALID_ARG;

    uint32_t frames_needed = sample_rate * duration_ms / 1000;
    uint32_t frames_sent = 0;
    uint32_t phase = 0;

    while (frames_sent < frames_needed) {
        for (int i = 0; i < BUFFER_LEN; i++) {
            int32_t sample = ((phase / half_period) % 2) ? amplitude : -amplitude;
            tone_buf[i * 2] = sample;
            tone_buf[i * 2 + 1] = sample;
            phase++;
        }
        esp_err_t err = i2s_channel_write(tx_handle, tone_buf, sizeof(tone_buf), &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) return err;
        frames_sent += BUFFER_LEN;
    }
    return ESP_OK;
}