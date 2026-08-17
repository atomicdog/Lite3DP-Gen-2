#include "slicer_detect.h"
#include "sd_card.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdbool.h>

static const char *TAG = "slicer";

#define PRUSA_DEFAULT_DIGITS  5

/* Split "<prefix><digits>.png" into its parts.
 * False when the name has no numeric suffix, or is *entirely* numeric —
 * "0.png" and "1.png" are the Voxeldance/Chitubox schemes and have their own
 * probes, so they must not be mistaken for a zero-length prefix here. */
static bool split_indexed_png(const char *name, size_t *out_prefix_len,
                              int *out_digits, long *out_index)
{
    size_t len = strlen(name);
    if (len < 5) return false;  /* shortest possible is "N.png" */
    if (strcasecmp(name + len - 4, ".png") != 0) return false;

    size_t base = len - 4;      /* characters before ".png" */
    size_t i = base;
    while (i > 0 && name[i - 1] >= '0' && name[i - 1] <= '9') {
        i--;
    }

    int digits = (int)(base - i);
    if (digits == 0) return false;  /* no index at all */
    if (i == 0) return false;       /* all digits — not a prefixed scheme */

    long index = 0;
    for (size_t k = i; k < base; k++) {
        index = index * 10 + (name[k] - '0');
    }

    *out_prefix_len = i;
    *out_digits = digits;
    *out_index = index;
    return true;
}

/* Recover the layer prefix by looking at what is actually on the card.
 * Confirms the guess by probing for index 0 rather than trusting the scan,
 * since sd_list_dir stops at SD_MAX_ENTRIES and may not have seen it. */
static bool discover_prefix(const char *folder_path, const char *folder_name,
                            char *out_prefix, size_t prefix_len, int *out_digits)
{
    /* 64 entries of 65 bytes is far too much for a task stack — the httpd
     * task has crashed on exactly this before. Job setup is not concurrent,
     * so a shared static is fine. */
    static sd_entry_t entries[SD_MAX_ENTRIES];
    int count = 0;

    if (sd_list_dir(folder_name, entries, SD_MAX_ENTRIES, &count) != ESP_OK) {
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;

        size_t plen;
        int digits;
        long index;
        if (!split_indexed_png(entries[i].name, &plen, &digits, &index)) continue;
        if (plen >= prefix_len) continue;

        char prefix[SLICER_PREFIX_MAX];
        memcpy(prefix, entries[i].name, plen);
        prefix[plen] = '\0';

        char probe[SD_MAX_PATH];
        snprintf(probe, sizeof(probe), "%s/%s%0*d.png", folder_path, prefix, digits, 0);
        if (!sd_file_exists(probe)) continue;

        memcpy(out_prefix, prefix, plen + 1);
        *out_digits = digits;
        return true;
    }
    return false;
}

esp_err_t slicer_detect(const char *folder_path, const char *folder_name,
                        slicer_type_t *out_type,
                        char *out_prefix, size_t prefix_len, int *out_digits)
{
    char test_path[SD_MAX_PATH];

    /* Defaults for the schemes that do not carry a prefix */
    if (out_prefix && prefix_len) out_prefix[0] = '\0';
    if (out_digits) *out_digits = 0;

    /* Test Prusa: <folder_name>00000.png */
    snprintf(test_path, sizeof(test_path), "%s/%s00000.png", folder_path, folder_name);
    if (sd_file_exists(test_path)) {
        *out_type = SLICER_PRUSA;
        if (out_prefix && prefix_len) {
            snprintf(out_prefix, prefix_len, "%s", folder_name);
        }
        if (out_digits) *out_digits = PRUSA_DEFAULT_DIGITS;
        ESP_LOGI(TAG, "Detected: PrusaSlicer");
        return ESP_OK;
    }

    /* Test Lychee 4-digit: lychee0000.png */
    snprintf(test_path, sizeof(test_path), "%s/lychee0000.png", folder_path);
    if (sd_file_exists(test_path)) {
        *out_type = SLICER_LYCHEE_4;
        ESP_LOGI(TAG, "Detected: Lychee (4-digit)");
        return ESP_OK;
    }

    /* Test Lychee 3-digit: lychee000.png */
    snprintf(test_path, sizeof(test_path), "%s/lychee000.png", folder_path);
    if (sd_file_exists(test_path)) {
        *out_type = SLICER_LYCHEE_3;
        ESP_LOGI(TAG, "Detected: Lychee (3-digit)");
        return ESP_OK;
    }

    /* Test Voxeldance: 0.png (0-indexed) */
    snprintf(test_path, sizeof(test_path), "%s/0.png", folder_path);
    if (sd_file_exists(test_path)) {
        *out_type = SLICER_VOXELDANCE;
        ESP_LOGI(TAG, "Detected: Voxeldance");
        return ESP_OK;
    }

    /* Test Chitubox: 1.png (1-indexed, no 0.png) */
    snprintf(test_path, sizeof(test_path), "%s/1.png", folder_path);
    if (sd_file_exists(test_path)) {
        *out_type = SLICER_CHITUBOX;
        ESP_LOGI(TAG, "Detected: Chitubox");
        return ESP_OK;
    }

    /* Last resort: the Prusa scheme with a prefix that is not the folder name.
     * Runs after every fixed-name probe so those keep reporting their own
     * slicer — "lychee0000.png" would otherwise match here as prefix
     * "lychee", producing identical paths under the wrong name. */
    if (out_prefix && prefix_len && out_digits &&
        discover_prefix(folder_path, folder_name, out_prefix, prefix_len, out_digits)) {
        *out_type = SLICER_PRUSA;
        /* ASCII only: non-ASCII in log output has broken idf_monitor here */
        ESP_LOGI(TAG, "Detected: PrusaSlicer (layer prefix '%s', %d digits, "
                      "does not match folder name '%s')",
                 out_prefix, *out_digits, folder_name);
        return ESP_OK;
    }

    *out_type = SLICER_UNKNOWN;
    ESP_LOGW(TAG, "Unknown slicer format in %s", folder_path);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t slicer_get_layer_path(slicer_type_t type, const char *folder_path,
                                const char *prefix, int digits, int layer_num,
                                char *out_path, size_t out_len)
{
    if (!out_path || out_len == 0) return ESP_ERR_INVALID_ARG;
    /* Never leave the caller's buffer uninitialized: an unknown type used
     * to hand back a stack full of garbage that then got logged and
     * fopen()ed. */
    out_path[0] = '\0';

    switch (type) {
    case SLICER_PRUSA:
        if (!prefix) return ESP_ERR_INVALID_ARG;
        /* A job built before detection stored a width would index every layer
         * as "...0.png" and fail on layer 0. */
        if (digits <= 0) digits = PRUSA_DEFAULT_DIGITS;
        snprintf(out_path, out_len, "%s/%s%0*d.png", folder_path, prefix, digits, layer_num);
        break;
    case SLICER_LYCHEE_4:
        snprintf(out_path, out_len, "%s/lychee%04d.png", folder_path, layer_num);
        break;
    case SLICER_LYCHEE_3:
        snprintf(out_path, out_len, "%s/lychee%03d.png", folder_path, layer_num);
        break;
    case SLICER_VOXELDANCE:
        snprintf(out_path, out_len, "%s/%d.png", folder_path, layer_num);
        break;
    case SLICER_CHITUBOX:
        /* Chitubox is 1-indexed */
        snprintf(out_path, out_len, "%s/%d.png", folder_path, layer_num + 1);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

const char *slicer_type_name(slicer_type_t type)
{
    switch (type) {
    case SLICER_PRUSA:      return "PrusaSlicer";
    case SLICER_LYCHEE_4:   return "Lychee";
    case SLICER_LYCHEE_3:   return "Lychee";
    case SLICER_VOXELDANCE: return "Voxeldance";
    case SLICER_CHITUBOX:   return "Chitubox";
    default:                return "Unknown";
    }
}
