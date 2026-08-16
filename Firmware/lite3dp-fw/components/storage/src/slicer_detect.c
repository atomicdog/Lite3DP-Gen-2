#include "slicer_detect.h"
#include "sd_card.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "slicer";

esp_err_t slicer_detect(const char *folder_path, const char *folder_name,
                         slicer_type_t *out_type)
{
    char test_path[SD_MAX_PATH];

    /* Test Prusa: <folder_name>00000.png */
    snprintf(test_path, sizeof(test_path), "%s/%s00000.png", folder_path, folder_name);
    if (sd_file_exists(test_path)) {
        *out_type = SLICER_PRUSA;
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

    *out_type = SLICER_UNKNOWN;
    ESP_LOGW(TAG, "Unknown slicer format in %s", folder_path);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t slicer_get_layer_path(slicer_type_t type, const char *folder_path,
                                const char *folder_name, int layer_num,
                                char *out_path, size_t out_len)
{
    if (!out_path || out_len == 0) return ESP_ERR_INVALID_ARG;
    /* Never leave the caller's buffer uninitialized: an unknown type used
     * to hand back a stack full of garbage that then got logged and
     * fopen()ed. */
    out_path[0] = '\0';

    switch (type) {
    case SLICER_PRUSA:
        snprintf(out_path, out_len, "%s/%s%05d.png", folder_path, folder_name, layer_num);
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
