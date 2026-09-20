#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "gpio.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

// Open file handle. Callers may stack-allocate this and pass its address around;
// only sd_card_open and sd_card_close should touch the fields.
typedef struct {
    FILE* fp;
} sd_file_t;

esp_err_t sd_card_init();
esp_err_t sd_card_deinit();

// Filenames are relative to the mount point, e.g. "beep.wav" or "/sounds/beep.wav".
esp_err_t sd_card_get_file_size(const char* filename, size_t* size);

// Reads up to buf_len bytes into a caller-provided buffer
esp_err_t sd_card_read_file_into(const char* filename, uint8_t* buf, size_t buf_len, size_t* bytes_read);

// Allocates a buffer holding the whole file (null terminated, not counted in size).
// Caller must free() *data.
esp_err_t sd_card_read_file(const char* filename, uint8_t** data, size_t* size);

// Streaming access, for files too large to hold in RAM. Every successful open
// must be paired with a close, which releases the underlying file descriptor.
esp_err_t sd_card_open(const char* filename, sd_file_t* file);
esp_err_t sd_card_close(sd_file_t* file);

// Reads up to buf_len bytes from the current position. A short read is not an
// error: *bytes_read comes back less than buf_len at end of file, and zero once
// there is nothing left.
esp_err_t sd_card_read(sd_file_t* file, uint8_t* buf, size_t buf_len, size_t* bytes_read);

// Moves the read position. whence takes the stdio constants SEEK_SET, SEEK_CUR
// and SEEK_END.
esp_err_t sd_card_seek(sd_file_t* file, long offset, int whence);

