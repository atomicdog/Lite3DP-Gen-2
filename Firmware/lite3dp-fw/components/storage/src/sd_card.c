#include "sd_card.h"
#include "hal_gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "sd_card";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t sd_card_init(void)
{
    if (s_mounted) return ESP_OK;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;  /* Same bus as TFT */

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_SD_CS;
    slot_cfg.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t sd_card_deinit(void)
{
    if (!s_mounted) return ESP_OK;

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (ret == ESP_OK) {
        s_mounted = false;
        s_card = NULL;
    }
    return ret;
}

bool sd_card_present(void)
{
    /* GPIO34 is SD detect — low when card is inserted */
    return gpio_get_level(PIN_SD_DETECT) == 0;
}

esp_err_t sd_list_dir(const char *path, sd_entry_t *entries, int max_count, int *out_count)
{
    char full_path[SD_MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);

    DIR *dir = opendir(full_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open dir: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        /* Skip hidden files */
        if (entry->d_name[0] == '.') continue;

        strncpy(entries[count].name, entry->d_name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].is_dir = (entry->d_type == DT_DIR);
        count++;
    }
    closedir(dir);

    *out_count = count;
    return ESP_OK;
}

esp_err_t sd_count_files(const char *dir_path, const char *extension, int *count)
{
    char full_path[SD_MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, dir_path);

    DIR *dir = opendir(full_path);
    if (!dir) return ESP_ERR_NOT_FOUND;

    *count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;
        if (extension) {
            size_t nlen = strlen(entry->d_name);
            size_t elen = strlen(extension);
            if (nlen < elen) continue;
            if (strcasecmp(entry->d_name + nlen - elen, extension) != 0) continue;
        }
        (*count)++;
    }
    closedir(dir);
    return ESP_OK;
}

bool sd_file_exists(const char *path)
{
    char full_path[SD_MAX_PATH];
    struct stat st;

    if (!path || !*path) return false;

    /* Accept both a path relative to the card root and one that already
     * carries the mount point. slicer_detect builds absolute paths from
     * job.folder_path, and prefixing those produced
     * "/sdcard//sdcard/JOB/..." — which never exists, so every slicer
     * probe failed and every job came back as SLICER_UNKNOWN. */
    if (strncmp(path, SD_MOUNT_POINT "/", strlen(SD_MOUNT_POINT "/")) == 0) {
        return stat(path, &st) == 0;
    }

    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);
    return stat(full_path, &st) == 0;
}
