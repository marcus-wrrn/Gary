#include "sd_card.h"
#include "esp_vfs_fat.h"

#define MAX_CHAR_SIZE 64
#define MOUNT_POINT "/sdcard"

static const char* TAG = "SD_CARD";

void sd_card_init() {
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();



}