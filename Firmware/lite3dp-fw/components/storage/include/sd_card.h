#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define SD_MOUNT_POINT  "/sdcard"
#define SD_MAX_PATH     128
#define SD_MAX_ENTRIES  64

typedef struct {
    char name[64];
    bool is_dir;
} sd_entry_t;

/** Mount the SD card (SPI mode). */
esp_err_t sd_card_init(void);

/** Unmount the SD card. */
esp_err_t sd_card_deinit(void);

/** Check if an SD card is physically present (via detect pin). */
bool sd_card_present(void);

/**
 * List directory entries (files and subdirectories).
 * @param path       Directory path relative to mount point
 * @param entries    Output array
 * @param max_count  Size of entries array
 * @param out_count  Number of entries found
 */
esp_err_t sd_list_dir(const char *path, sd_entry_t *entries, int max_count, int *out_count);

/**
 * Count files matching a pattern in a directory.
 * @param dir_path   Directory path relative to mount point
 * @param extension  File extension to match (e.g., ".png"), or NULL for all
 */
esp_err_t sd_count_files(const char *dir_path, const char *extension, int *count);

/** Check if a file exists. */
/**
 * True if the file exists. Accepts either a path relative to the card
 * root ("JOB/layer.png") or one already including the mount point
 * ("/sdcard/JOB/layer.png").
 */
bool sd_file_exists(const char *path);
