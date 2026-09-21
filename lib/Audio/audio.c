#include "audio.h"
#include <stdbool.h>
#include <string.h>
#include "sd_card.h"
#include "i2s.h"
#include "esp_log.h"
#include "esp_timer.h"

#define STREAM_BUFFER_SIZE  4096
#define MIC_FRAMES_PER_READ 256
#define MIC_LOG_INTERVAL_US (1000 * 1000)
#define RIFF_HEADER_SIZE    12
#define CHUNK_HEADER_SIZE   8
#define FMT_CHUNK_MIN_SIZE  16
#define WAVE_FORMAT_PCM     1

static const char* TAG = "AUDIO";
static uint8_t stream_buf[STREAM_BUFFER_SIZE];

// Both WS slots are measured because only the one the mic is strapped to
// carries data; the other floats, and comparing the two is what identifies
// which is which without having to inspect the wiring.
typedef struct {
    int32_t peak;
    int64_t total;
    uint32_t dead;
} slot_stats_t;

static int32_t mic_buf[MIC_FRAMES_PER_READ * 2];
static bool mic_running = false;
static int64_t mic_window_start_us = 0;
static slot_stats_t mic_left;
static slot_stats_t mic_right;
static uint32_t mic_frames = 0;

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

static esp_err_t play_wav(const char* filename) {
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

    ret = i2s_enable_tx();
    if (ret != ESP_OK) {
        sd_card_close(&file);
        return ret;
    }

    ret = stream_samples(&file, &info);

    esp_err_t disable_ret = i2s_disable_tx();
    esp_err_t close_ret = sd_card_close(&file);

    if (ret != ESP_OK) return ret;
    if (disable_ret != ESP_OK) return disable_ret;
    return close_ret;
}

static void mic_reset_window() {
    memset(&mic_left, 0, sizeof(mic_left));
    memset(&mic_right, 0, sizeof(mic_right));
    mic_frames = 0;
    mic_window_start_us = esp_timer_get_time();
}

static void mic_accumulate(slot_stats_t* stats, int32_t word) {
    // The mic left-justifies its 24 bits in the slot, so the low byte is
    // padding that has to be shifted off before the value means anything.
    int32_t sample = word >> 8;
    int32_t magnitude = (sample < 0) ? -sample : sample;

    if (magnitude > stats->peak) {
        stats->peak = magnitude;
    }
    stats->total += magnitude;

    // An undriven slot reads as all zeros or all ones, so counting the samples
    // that sit at those values separates a silent mic from an absent one.
    if (magnitude <= 1) {
        stats->dead++;
    }
}

esp_err_t mic_start() {
    if (mic_running) {
        return ESP_OK;
    }

    // Retuning needs the bus idle, and the mic then needs TX running to be
    // clocked at all. TX sends silence the whole time it listens.
    esp_err_t ret = i2s_disable_tx();
    if (ret != ESP_OK) {
        return ret;
    }

    // Stereo rather than mono: RX mono still writes both WS slots into DMA on
    // this chip, so asking for the pair up front makes the frame layout plain
    // and lets the unused slot be dropped deliberately when counting.
    ret = i2s_configure_stream(MIC_SAMPLE_RATE, MIC_BIT_DEPTH, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2s_enable_tx();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2s_enable_rx();
    if (ret != ESP_OK) {
        i2s_disable_tx();
        return ret;
    }

    mic_reset_window();
    mic_running = true;
    ESP_LOGI(TAG, "Mic listening: %u Hz, %u-bit, left slot",
             (unsigned)MIC_SAMPLE_RATE, (unsigned)MIC_BIT_DEPTH);
    return ESP_OK;
}

esp_err_t mic_stop() {
    if (!mic_running) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_disable_rx();
    if (ret != ESP_OK) {
        return ret;
    }

    // The clock was only running for the mic's benefit, so it stops too.
    ret = i2s_disable_tx();
    if (ret != ESP_OK) {
        return ret;
    }

    mic_running = false;
    return ESP_OK;
}

bool mic_is_running() {
    return mic_running;
}

esp_err_t mic_poll() {
    if (!mic_running) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read;
    esp_err_t ret = i2s_read(mic_buf, sizeof(mic_buf), &bytes_read);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t frames = bytes_read / (sizeof(int32_t) * 2);
    for (size_t i = 0; i < frames; i++) {
        mic_accumulate(&mic_left, mic_buf[i * 2]);
        mic_accumulate(&mic_right, mic_buf[i * 2 + 1]);
    }
    mic_frames += frames;


    if (esp_timer_get_time() - mic_window_start_us < MIC_LOG_INTERVAL_US) {
        return ESP_OK;
    }

    if (mic_frames > 0) {
        ESP_LOGI(TAG, "Mic %u frames | L: peak %ld avg %ld dead %u%% | R: peak %ld avg %ld dead %u%%",
                 (unsigned)mic_frames,
                 (long)mic_left.peak, (long)(mic_left.total / mic_frames),
                 (unsigned)(mic_left.dead * 100 / mic_frames),
                 (long)mic_right.peak, (long)(mic_right.total / mic_frames),
                 (unsigned)(mic_right.dead * 100 / mic_frames));
    } else {
        ESP_LOGW(TAG, "Mic returned no frames");
    }

    mic_reset_window();
    return ESP_OK;
}

esp_err_t play_track(const char* filename) {
    // Retuning the shared clock means stopping RX first, so a mic that was
    // listening gets paused for the length of the track and resumed after.
    bool resume_mic = mic_running;
    if (resume_mic) {
        esp_err_t stop_ret = mic_stop();
        if (stop_ret != ESP_OK) {
            return stop_ret;
        }
    }

    esp_err_t ret = play_wav(filename);

    if (resume_mic) {
        esp_err_t start_ret = mic_start();
        if (ret == ESP_OK) {
            ret = start_ret;
        }
    }
    return ret;
}
