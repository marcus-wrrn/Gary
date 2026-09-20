#include "audio.h"
#include <stdbool.h>
#include <string.h>
#include "sd_card.h"
#include "i2s.h"
#include "esp_log.h"

#define STREAM_BUFFER_SIZE  4096
#define RIFF_HEADER_SIZE    12
#define CHUNK_HEADER_SIZE   8
#define FMT_CHUNK_MIN_SIZE  16
#define WAVE_FORMAT_PCM     1

static const char* TAG = "AUDIO";
static uint8_t stream_buf[STREAM_BUFFER_SIZE];

typedef struct {
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t data_size;    
} wav_info_t;

static uint16_t read_le16(const uint8_t* buf) {
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

static uint32_t read_le32(const uint8_t* buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static esp_err_t read_exact(sd_file_t* file, uint8_t* buf, size_t len) {
    size_t bytes_read;
    esp_err_t ret = sd_card_read(file, buf, len, &bytes_read);
    if (ret != ESP_OK) {
        return ret;
    }
    if (bytes_read != len) {
        ESP_LOGE(TAG, "Unexpected end of file");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// Walks the RIFF chunk list looking for "fmt " and "data". The header is not a
// fixed 44 bytes
static esp_err_t parse_wav_header(sd_file_t* file, wav_info_t* info) {
    uint8_t buf[FMT_CHUNK_MIN_SIZE];

    esp_err_t ret = read_exact(file, buf, RIFF_HEADER_SIZE);
    if (ret != ESP_OK) {
        return ret;
    }
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a RIFF/WAVE file");
        return ESP_ERR_NOT_SUPPORTED;
    }

    bool have_fmt = false;
    while (1) {
        ret = read_exact(file, buf, CHUNK_HEADER_SIZE);
        if (ret != ESP_OK) {
            return ret;
        }

        char chunk_id[5];
        memcpy(chunk_id, buf, 4);
        chunk_id[4] = '\0';
        uint32_t chunk_size = read_le32(buf + 4);

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < FMT_CHUNK_MIN_SIZE) {
                ESP_LOGE(TAG, "Malformed fmt chunk");
                return ESP_ERR_NOT_SUPPORTED;
            }

            ret = read_exact(file, buf, FMT_CHUNK_MIN_SIZE);
            if (ret != ESP_OK) {
                return ret;
            }

            uint16_t audio_format = read_le16(buf);
            if (audio_format != WAVE_FORMAT_PCM) {
                ESP_LOGE(TAG, "Only uncompressed PCM is supported (format %u)", (unsigned)audio_format);
                return ESP_ERR_NOT_SUPPORTED;
            }

            info->num_channels = read_le16(buf + 2);
            info->sample_rate = read_le32(buf + 4);
            info->block_align = read_le16(buf + 12);
            info->bits_per_sample = read_le16(buf + 14);
            have_fmt = true;

            // An extended fmt chunk carries fields past the 16 we read.
            if (chunk_size > FMT_CHUNK_MIN_SIZE) {
                ret = sd_card_seek(file, chunk_size - FMT_CHUNK_MIN_SIZE, SEEK_CUR);
                if (ret != ESP_OK) {
                    return ret;
                }
            }
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            if (!have_fmt) {
                ESP_LOGE(TAG, "data chunk precedes fmt chunk");
                return ESP_ERR_NOT_SUPPORTED;
            }
            info->data_size = chunk_size;
            return ESP_OK;
        } else {
            long skip = chunk_size + (chunk_size & 1);
            ret = sd_card_seek(file, skip, SEEK_CUR);
            if (ret != ESP_OK) {
                return ret;
            }
        }
    }
}

static esp_err_t stream_samples(sd_file_t* file, const wav_info_t* info) {
    // Never hand I2S a partial frame, even if the final read comes up short.
    size_t chunk_limit = STREAM_BUFFER_SIZE - (STREAM_BUFFER_SIZE % info->block_align);
    uint32_t remaining = info->data_size;

    while (remaining > 0) {
        size_t to_read = (remaining < chunk_limit) ? remaining : chunk_limit;

        size_t bytes_read;
        esp_err_t ret = sd_card_read(file, stream_buf, to_read, &bytes_read);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_read == 0) {
            // The data chunk claimed more bytes than the file actually holds.
            ESP_LOGW(TAG, "File ended %u bytes early", (unsigned)remaining);
            break;
        }
        remaining -= bytes_read;

        size_t playable = bytes_read - (bytes_read % info->block_align);
        if (playable == 0) {
            break;
        }

        size_t bytes_written;
        ret = i2s_write(stream_buf, playable, &bytes_written);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t play_track(const char* filename) {
    sd_file_t file;
    esp_err_t ret = sd_card_open(filename, &file);
    if (ret != ESP_OK) {
        return ret;
    }

    wav_info_t info;
    ret = parse_wav_header(&file, &info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to parse WAV header: %s", filename);
        sd_card_close(&file);
        return ret;
    }

    if (info.block_align == 0 ||
        info.block_align != info.num_channels * (info.bits_per_sample / 8)) {
        ESP_LOGE(TAG, "Inconsistent block alignment in %s", filename);
        sd_card_close(&file);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Playing %s: %u Hz, %u-bit, %u ch, %u bytes",
             filename, (unsigned)info.sample_rate, (unsigned)info.bits_per_sample,
             (unsigned)info.num_channels, (unsigned)info.data_size);

    // Retuning the channel to the file's own format avoids resampling entirely,
    // so the bytes off the card go straight out to the amplifier.
    ret = i2s_configure_stream(info.sample_rate, info.bits_per_sample, info.num_channels);
    if (ret != ESP_OK) {
        sd_card_close(&file);
        return ret;
    }

    ret = i2s_enable_channel();
    if (ret != ESP_OK) {
        sd_card_close(&file);
        return ret;
    }

    ret = stream_samples(&file, &info);

    esp_err_t disable_ret = i2s_disable_channel();
    esp_err_t close_ret = sd_card_close(&file);

    if (ret != ESP_OK) return ret;
    if (disable_ret != ESP_OK) return disable_ret;
    return close_ret;
}
