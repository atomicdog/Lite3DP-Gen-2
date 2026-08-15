#pragma once

#include "esp_err.h"
#include <stddef.h>

typedef enum {
    SLICER_UNKNOWN,
    SLICER_PRUSA,       /* <folder><00000>.png */
    SLICER_LYCHEE_4,    /* lychee<0000>.png    (4-digit) */
    SLICER_LYCHEE_3,    /* lychee<000>.png     (3-digit) */
    SLICER_VOXELDANCE,  /* 0.png, 1.png, ...   (0-indexed) */
    SLICER_CHITUBOX,    /* 1.png, 2.png, ...   (1-indexed) */
} slicer_type_t;

/**
 * Detect the slicer format used in a directory by probing for characteristic filenames.
 * @param folder_path  Full path to the folder on SD card
 * @param folder_name  Just the folder name (used for Prusa pattern)
 * @param out_type     Detected slicer type
 */
esp_err_t slicer_detect(const char *folder_path, const char *folder_name, slicer_type_t *out_type);

/**
 * Generate the filename for a given layer number.
 * @param type         Slicer format
 * @param folder_path  Full path to the folder
 * @param folder_name  Folder name (for Prusa pattern)
 * @param layer_num    Layer number (0-indexed internally)
 * @param out_path     Output buffer for full file path
 * @param out_len      Size of output buffer
 */
esp_err_t slicer_get_layer_path(slicer_type_t type, const char *folder_path,
                                const char *folder_name, int layer_num,
                                char *out_path, size_t out_len);

/** Return a human-readable name for the slicer type. */
const char *slicer_type_name(slicer_type_t type);
