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

/** Longest layer-filename prefix we will carry around. */
#define SLICER_PREFIX_MAX  64

/**
 * Detect the slicer format used in a directory by probing for characteristic filenames.
 *
 * PrusaSlicer names layer files after the *project*, which is not necessarily
 * the folder name — renaming the folder (or exporting into a differently named
 * one) breaks the obvious <folder_name><00000>.png probe. When that probe
 * fails and no other scheme matches, the folder is scanned and the prefix is
 * taken from a real file, so such a job still prints.
 *
 * @param folder_path  Full path to the folder on SD card
 * @param folder_name  Just the folder name (first guess at the Prusa prefix)
 * @param out_type     Detected slicer type
 * @param out_prefix   Receives the layer filename prefix (Prusa scheme only;
 *                     empty for schemes with a fixed name). May be NULL.
 * @param prefix_len   Size of out_prefix, normally SLICER_PREFIX_MAX
 * @param out_digits   Receives the zero-padded index width (Prusa scheme
 *                     only). May be NULL.
 */
esp_err_t slicer_detect(const char *folder_path, const char *folder_name,
                        slicer_type_t *out_type,
                        char *out_prefix, size_t prefix_len, int *out_digits);

/**
 * Generate the filename for a given layer number.
 * @param type         Slicer format
 * @param folder_path  Full path to the folder
 * @param prefix       Layer filename prefix from slicer_detect (Prusa pattern)
 * @param digits       Zero-padded index width from slicer_detect
 * @param layer_num    Layer number (0-indexed internally)
 * @param out_path     Output buffer for full file path
 * @param out_len      Size of output buffer
 */
esp_err_t slicer_get_layer_path(slicer_type_t type, const char *folder_path,
                                const char *prefix, int digits, int layer_num,
                                char *out_path, size_t out_len);

/** Return a human-readable name for the slicer type. */
const char *slicer_type_name(slicer_type_t type);
