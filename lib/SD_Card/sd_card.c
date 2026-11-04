#include "sd_card.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"

static const char* TAG = "SD_CARD";
static sdmmc_card_t* card = NULL;

esp_err_t sd_card_init() {
    if (card != NULL) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 1;
    slot_cfg.clk = SDMMC_CLK_PIN;
    slot_cfg.cmd = SDMMC_CMD_PIN;
    slot_cfg.d0 = SDMMC_D0_PIN;
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to mount SD card (%s)", esp_err_to_name(ret));
        card = NULL;
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

esp_err_t sd_card_deinit() {
    if (card == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to unmount SD card");
        return ret;
    }
    card = NULL;
    return ESP_OK;
}

static esp_err_t build_path(const char* filename, char* path, size_t path_len) {
    if (card == NULL) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    if (filename == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* sep = (filename[0] == '/') ? "" : "/";
    int len = snprintf(path, path_len, "%s%s%s", SD_CARD_MOUNT_POINT, sep, filename);
    if (len < 0 || (size_t)len >= path_len) {
        ESP_LOGE(TAG, "Path too long: %s", filename);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t sd_card_get_file_size(const char* filename, size_t* size) {
    if (size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[MAX_PATH_SIZE];
    esp_err_t ret = build_path(filename, path, sizeof(path));
    if (ret != ESP_OK) {
        return ret;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    *size = (size_t)st.st_size;
    return ESP_OK;
}

esp_err_t sd_card_find_file(const char* dir, const char* ext, char* name, size_t name_len) {
    if (ext == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[MAX_PATH_SIZE];
    esp_err_t ret = build_path(dir, path, sizeof(path));
    if (ret != ESP_OK) {
        return ret;
    }

    DIR* d = opendir(path);
    if (d == NULL) {
        ESP_LOGE(TAG, "Unable to open directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t ext_len = strlen(ext);
    const char* sep = (dir[0] != '\0' && dir[strlen(dir) - 1] == '/') ? "" : "/";
    ret = ESP_ERR_NOT_FOUND;

    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        size_t len = strlen(entry->d_name);
        if (len < ext_len || strcasecmp(entry->d_name + len - ext_len, ext) != 0) {
            continue;
        }

        int written = snprintf(name, name_len, "%s%s%s", dir, sep, entry->d_name);
        ret = (written < 0 || (size_t)written >= name_len) ? ESP_ERR_INVALID_SIZE : ESP_OK;
        break;
    }

    closedir(d);
    return ret;
}

esp_err_t sd_card_read_file_into(const char* filename, uint8_t* buf, size_t buf_len, size_t* bytes_read) {
    if (buf == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[MAX_PATH_SIZE];
    esp_err_t ret = build_path(filename, path, sizeof(path));
    if (ret != ESP_OK) {
        return ret;
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Unable to open file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    *bytes_read = fread(buf, 1, buf_len, f);
    ret = ferror(f) ? ESP_FAIL : ESP_OK;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read file: %s", path);
    }

    fclose(f);
    return ret;
}

esp_err_t sd_card_read_file(const char* filename, uint8_t** data, size_t* size) {
    if (data == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *data = NULL;
    *size = 0;

    size_t file_size;
    esp_err_t ret = sd_card_get_file_size(filename, &file_size);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t* buf = malloc(file_size + 1);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to allocate %u bytes for %s", (unsigned)file_size, filename);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read;
    ret = sd_card_read_file_into(filename, buf, file_size, &bytes_read);
    if (ret != ESP_OK || bytes_read != file_size) {
        free(buf);
        return (ret != ESP_OK) ? ret : ESP_FAIL;
    }

    buf[file_size] = '\0';
    *data = buf;
    *size = file_size;
    return ESP_OK;
}

esp_err_t sd_card_open(const char* filename, sd_file_t* file) {
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    file->fp = NULL;

    char path[MAX_PATH_SIZE];
    esp_err_t ret = build_path(filename, path, sizeof(path));
    if (ret != ESP_OK) {
        return ret;
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Unable to open file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    file->fp = f;
    return ESP_OK;
}

esp_err_t sd_card_close(sd_file_t* file) {
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (file->fp == NULL) {
        return ESP_OK;
    }

    int ret = fclose(file->fp);
    file->fp = NULL;
    if (ret != 0) {
        ESP_LOGE(TAG, "Unable to close file");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sd_card_read(sd_file_t* file, uint8_t* buf, size_t buf_len, size_t* bytes_read) {
    if (file == NULL || file->fp == NULL || buf == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *bytes_read = fread(buf, 1, buf_len, file->fp);
    // A short read means end of file unless the error flag is also set, so ferror
    // is the only thing that distinguishes the two.
    if (ferror(file->fp)) {
        ESP_LOGE(TAG, "Unable to read from file");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sd_card_seek(sd_file_t* file, long offset, int whence) {
    if (file == NULL || file->fp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fseek(file->fp, offset, whence) != 0) {
        ESP_LOGE(TAG, "Unable to seek to offset %ld", offset);
        return ESP_FAIL;
    }
    return ESP_OK;
}
