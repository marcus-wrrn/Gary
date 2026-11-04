#include "audio.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "sd_card.h"
#include "fft.h"
#include "direction.h"
#include "i2s.h"
#include "esp_log.h"
#include "esp_timer.h"

#define STREAM_BUFFER_SIZE  4096
#define MIC_FRAMES_PER_READ 256
#define MIC_FFT_SIZE        MIC_FRAMES_PER_READ
#define MIC_FULL_SCALE      8388608.0f

#define MIC_SPECTRUM_FLOOR_DB   (-75.0f)
#define MIC_SPECTRUM_CEIL_DB    (-15.0f)

#define MIC_LEVEL_FLOOR_DB      (-40.0f)
#define MIC_LEVEL_CEIL_DB       (-30.0f)
// Set to 0 to stop logging every mic's level, for checking each one is wired and hearing
#define MIC_DEBUG_LOG           1
#define MIC_DEBUG_LOG_US        (500 * 1000)
#define RIFF_HEADER_SIZE    12
#define CHUNK_HEADER_SIZE   8
#define FMT_CHUNK_MIN_SIZE  16
#define WAVE_FORMAT_PCM     1

static const char* TAG = "AUDIO";
static uint8_t stream_buf[STREAM_BUFFER_SIZE];

static int32_t mic_buf[MIC_FRAMES_PER_READ * 2];
// The third mic's controller, same layout, with the mic on the left slot
static int32_t mic_aux_buf[MIC_FRAMES_PER_READ * 2];
static float mic_fft_re[MIC_FFT_SIZE];
static float mic_fft_im[MIC_FFT_SIZE];
static float mic_fft_mags[MIC_FFT_SIZE / 2];
// First bin of each band plus a closing edge, so band b covers [edges[b], edges[b + 1])
static uint8_t mic_band_edges[MIC_BAND_COUNT + 1];

// Written by mic_poll() on the sound task and read by whoever is drawing, so
// both sides take the spinlock; the copy is small enough to do inside it.
static portMUX_TYPE mic_spectrum_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t mic_spectrum[MIC_BAND_COUNT];
static uint8_t mic_level_left;
static uint8_t mic_level_right;
static bool mic_spectrum_valid = false;

static bool mic_running = false;
static bool mic_has_aux = false;

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

// Log-spaced band edges over bins 1..MIC_FFT_SIZE / 2, skipping bin 0 because
// DC carries the mic's standing offset rather than anything audible.
static void mic_build_bands() {
    const float lowest = 1.0f;
    const float highest = (float)(MIC_FFT_SIZE / 2);

    for (size_t i = 0; i <= MIC_BAND_COUNT; i++) {
        float edge = lowest * powf(highest / lowest, (float)i / (float)MIC_BAND_COUNT);
        size_t bin = (size_t)lroundf(edge);
        if (i > 0 && bin <= mic_band_edges[i - 1]) {
            bin = (size_t)mic_band_edges[i - 1] + 1;
        }
        mic_band_edges[i] = (uint8_t)bin;
    }
}

static float mic_db_to_unit(float db, float floor_db, float ceil_db) {
    float t = (db - floor_db) / (ceil_db - floor_db);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

// RMS of one slot over the block, with the block mean taken off first so the
// mic's standing DC offset doesn't read as sound.
static uint8_t mic_slot_level(size_t slot, size_t frames) {
    float mean = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        mean += (float)(mic_buf[i * 2 + slot] >> 8);
    }
    mean /= (float)frames;

    float sum_sq = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        float sample = ((float)(mic_buf[i * 2 + slot] >> 8) - mean) / MIC_FULL_SCALE;
        sum_sq += sample * sample;
    }

    float db = 10.0f * log10f(sum_sq / (float)frames + 1e-18f);
    return (uint8_t)lroundf(mic_db_to_unit(db, MIC_LEVEL_FLOOR_DB, MIC_LEVEL_CEIL_DB) * 255.0f);
}

#if MIC_DEBUG_LOG
// RMS in dB relative to full scale, the peak as a fraction of full scale, and
// the first raw sample. A mic that isn't driving its line tends to show the
// same raw value every block, often 0 or -1.
typedef struct {
    float db;
    float peak;
    int32_t raw;
} mic_debug_t;

static mic_debug_t mic_debug_measure(const int32_t* buf, size_t slot, size_t frames) {
    float mean = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        mean += (float)(buf[i * 2 + slot] >> 8);
    }
    mean /= (float)frames;

    float sum_sq = 0.0f;
    float peak = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        float sample = ((float)(buf[i * 2 + slot] >> 8) - mean) / MIC_FULL_SCALE;
        sum_sq += sample * sample;
        if (fabsf(sample) > peak) peak = fabsf(sample);
    }

    mic_debug_t out = {
        .db = 10.0f * log10f(sum_sq / (float)frames + 1e-18f),
        .peak = peak,
        .raw = buf[slot] >> 8,
    };
    return out;
}

static void mic_debug_log(const int32_t* aux, size_t frames) {
    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();
    if (frames == 0 || now - last_us < MIC_DEBUG_LOG_US) {
        return;
    }
    last_us = now;

    mic_debug_t slot0 = mic_debug_measure(mic_buf, 0, frames);
    mic_debug_t slot1 = mic_debug_measure(mic_buf, 1, frames);
    if (aux == NULL) {
        ESP_LOGI(TAG, "slot0 %6.1f dB pk %.3f raw %8ld | slot1 %6.1f dB pk %.3f raw %8ld | aux %s",
                 slot0.db, slot0.peak, (long)slot0.raw, slot1.db, slot1.peak, (long)slot1.raw,
                 mic_has_aux ? "short read" : "none");
        return;
    }
    // Both aux slots, since a mic with its L/R pin strapped the wrong way talks on slot 1
    mic_debug_t aux0 = mic_debug_measure(aux, 0, frames);
    mic_debug_t aux1 = mic_debug_measure(aux, 1, frames);
    ESP_LOGI(TAG, "slot0 %6.1f dB pk %.3f raw %8ld | slot1 %6.1f dB pk %.3f raw %8ld | "
                  "aux0 %6.1f dB pk %.3f raw %8ld | aux1 %6.1f dB pk %.3f raw %8ld",
             slot0.db, slot0.peak, (long)slot0.raw, slot1.db, slot1.peak, (long)slot1.raw,
             aux0.db, aux0.peak, (long)aux0.raw, aux1.db, aux1.peak, (long)aux1.raw);
}
#endif

// Transforms one full block and replaces the snapshot. A short block is left
// alone rather than zero-padded, since the last snapshot is a better answer
// than a spectrum smeared by the padding.
static void mic_update_spectrum(size_t frames) {
    if (frames < MIC_FFT_SIZE) {
        return;
    }

    for (size_t i = 0; i < MIC_FFT_SIZE; i++) {
        // Both slots are folded together so the spectrum hears the whole room
        // rather than one side of it.
        float left = (float)(mic_buf[i * 2] >> 8);
        float right = (float)(mic_buf[i * 2 + 1] >> 8);
        mic_fft_re[i] = (left + right) * (0.5f / MIC_FULL_SCALE);
    }

    fft_magnitudes(mic_fft_re, mic_fft_im, mic_fft_mags, MIC_FFT_SIZE);

    uint8_t bands[MIC_BAND_COUNT];
    for (size_t b = 0; b < MIC_BAND_COUNT; b++) {
        // Peak rather than average across the band, so a single strong tone
        // still shows at full height instead of being diluted by its neighbours.
        float peak = 0.0f;
        for (size_t bin = mic_band_edges[b]; bin < mic_band_edges[b + 1]; bin++) {
            if (mic_fft_mags[bin] > peak) {
                peak = mic_fft_mags[bin];
            }
        }

        // A Hann window halves the amplitude and the transform spreads it over
        // n / 2 bins, so this scaling puts a full-scale sine at 0 dB.
        float level = peak * (4.0f / (float)MIC_FFT_SIZE);
        float db = 20.0f * log10f(level + 1e-9f);
        float t = mic_db_to_unit(db, MIC_SPECTRUM_FLOOR_DB, MIC_SPECTRUM_CEIL_DB);
        bands[b] = (uint8_t)lroundf(t * 255.0f);
    }

    uint8_t left = mic_slot_level(0, frames);
    uint8_t right = mic_slot_level(1, frames);

    taskENTER_CRITICAL(&mic_spectrum_lock);
    memcpy(mic_spectrum, bands, sizeof(bands));
    mic_level_left = left;
    mic_level_right = right;
    mic_spectrum_valid = true;
    taskEXIT_CRITICAL(&mic_spectrum_lock);
}

esp_err_t mic_get_spectrum(uint8_t* bands, size_t count) {
    if (bands == NULL || count != MIC_BAND_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    taskENTER_CRITICAL(&mic_spectrum_lock);
    if (mic_spectrum_valid) {
        memcpy(bands, mic_spectrum, sizeof(mic_spectrum));
    } else {
        ret = ESP_ERR_INVALID_STATE;
    }
    taskEXIT_CRITICAL(&mic_spectrum_lock);
    return ret;
}

esp_err_t mic_get_levels(uint8_t* left, uint8_t* right) {
    if (left == NULL || right == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    taskENTER_CRITICAL(&mic_spectrum_lock);
    if (mic_spectrum_valid) {
        *left = mic_level_left;
        *right = mic_level_right;
    } else {
        ret = ESP_ERR_INVALID_STATE;
    }
    taskEXIT_CRITICAL(&mic_spectrum_lock);
    return ret;
}

esp_err_t mic_start() {
    if (mic_running) {
        return ESP_OK;
    }

    // Retuning needs the bus idle, and the mic then needs TX running to be
    // clocked at all. TX sends silence the whole time it listens.
    esp_err_t ret = i2s_stop_capture();
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

    ret = i2s_start_capture();
    if (ret != ESP_OK) {
        return ret;
    }

    mic_has_aux = i2s_has_aux();
    mic_build_bands();
    direction_reset();
    mic_running = true;
    ESP_LOGI(TAG, "Mic listening: %u Hz, %u-bit, %s",
             (unsigned)MIC_SAMPLE_RATE, (unsigned)MIC_BIT_DEPTH,
             mic_has_aux ? "3 mics" : "2 mics, no aux");
    return ESP_OK;
}

esp_err_t mic_stop() {
    if (!mic_running) {
        return ESP_OK;
    }

    // The clock was only running for the mic's benefit, so it stops too.
    esp_err_t ret = i2s_stop_capture();
    if (ret != ESP_OK) {
        return ret;
    }

    mic_running = false;

    // Handing out the last snapshot after the mic stops would freeze a drawing
    // caller on whatever it happened to hear last.
    taskENTER_CRITICAL(&mic_spectrum_lock);
    mic_spectrum_valid = false;
    taskEXIT_CRITICAL(&mic_spectrum_lock);
    direction_reset();
    return ESP_OK;
}

bool mic_is_running() {
    return mic_running;
}

esp_err_t mic_poll() {
    if (!mic_running) {
        return ESP_ERR_INVALID_STATE;
    }

    // A dropped block on either controller leaves one stream ahead of the
    // other for good, which would read as a steady false delay, so both are
    // restarted together to line them back up.
    if (i2s_capture_overflowed()) {
        ESP_LOGW(TAG, "Mic fell behind, restarting capture to resync");
        esp_err_t ret = i2s_stop_capture();
        if (ret == ESP_OK) {
            ret = i2s_start_capture();
        }
        direction_reset();
        return ret;
    }

    size_t bytes_read;
    esp_err_t ret = i2s_read(mic_buf, sizeof(mic_buf), &bytes_read);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t frames = bytes_read / (sizeof(int32_t) * 2);

    // Read in the same length as the main block, so frame n of each lines up
    const int32_t* aux = NULL;
    if (mic_has_aux) {
        size_t aux_bytes;
        ret = i2s_read_aux(mic_aux_buf, bytes_read, &aux_bytes);
        if (ret != ESP_OK) {
            return ret;
        }
        if (aux_bytes == bytes_read) {
            aux = mic_aux_buf;
        }
    }

    mic_update_spectrum(frames);
    direction_process(mic_buf, aux, frames);
#if MIC_DEBUG_LOG
    mic_debug_log(aux, frames);
#endif

    return ESP_OK;
}

// Retuning the shared clock means stopping RX first, so a mic that was
// listening gets paused for the length of the playback and resumed after.
static esp_err_t pause_mic(bool* resume_mic) {
    *resume_mic = mic_running;
    return mic_running ? mic_stop() : ESP_OK;
}

static esp_err_t resume_mic_after(bool resume_mic, esp_err_t ret) {
    if (resume_mic) {
        esp_err_t start_ret = mic_start();
        if (ret == ESP_OK) {
            ret = start_ret;
        }
    }
    return ret;
}

esp_err_t play_track(const char* filename) {
    bool resume_mic;
    esp_err_t ret = pause_mic(&resume_mic);
    if (ret != ESP_OK) {
        return ret;
    }
    return resume_mic_after(resume_mic, play_wav(filename));
}

esp_err_t play_beep(uint32_t freq_hz, uint32_t duration_ms) {
    bool resume_mic;
    esp_err_t ret = pause_mic(&resume_mic);
    if (ret != ESP_OK) {
        return ret;
    }
    return resume_mic_after(resume_mic, i2s_play_beep(freq_hz, duration_ms));
}
