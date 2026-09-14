#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gpio.h"

#define SD_CARD_MOUNT_POINT "/plant"

esp_err_t sd_card_init();
esp_err_t sd_card_deinit();

// Filenames are relative to the mount point, e.g. "beep.wav" or "/sounds/beep.wav".
esp_err_t sd_card_get_file_size(const char* filename, size_t* size);

// Reads up to buf_len bytes into a caller-provided buffer
esp_err_t sd_card_read_file_into(const char* filename, uint8_t* buf, size_t buf_len, size_t* bytes_read);

// Allocates a buffer holding the whole file (null terminated, not counted in size).
// Caller must free() *data.
esp_err_t sd_card_read_file(const char* filename, uint8_t** data, size_t* size);
