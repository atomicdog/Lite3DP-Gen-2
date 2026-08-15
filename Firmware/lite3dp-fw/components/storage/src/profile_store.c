#include "profile_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "profile";

#define NVS_NAMESPACE   "lite3dp"
#define NVS_KEY_PREFIX  "prof"  /* prof0, prof1, ..., prof6 */

void profile_defaults(print_profile_t *p)
{
    p->layer_height        = 0.1f;
    p->lift_height         = 4.0f;
    p->lift_height_initial = 6.0f;
    p->bottom_layers       = 4;
    p->transition_layers   = 0;
    p->exposure_time       = 6.0f;
    p->bottom_exposure     = 40;
    p->lift_speed          = 1.7f;
    p->lift_speed_initial  = 0.7f;
    p->retract_speed       = 2.9f;
    p->rest_time_ms        = 0;
    p->calibration_offset  = 0;
    p->uv_power            = 255;
}

esp_err_t profile_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t profile_load(int slot, print_profile_t *profile)
{
    if (slot < 0 || slot >= PROFILE_SLOT_COUNT) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, using defaults");
        profile_defaults(profile);
        return ESP_OK;
    }

    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, slot);

    size_t len = sizeof(print_profile_t);
    ret = nvs_get_blob(nvs, key, profile, &len);
    nvs_close(nvs);

    if (ret != ESP_OK || len != sizeof(print_profile_t)) {
        ESP_LOGW(TAG, "Profile slot %d not found, using defaults", slot);
        profile_defaults(profile);
        return ESP_OK;  /* Not an error — just use defaults */
    }

    ESP_LOGI(TAG, "Loaded profile slot %d (layer=%.3fmm, expo=%.1fs)", slot,
             (double)profile->layer_height, (double)profile->exposure_time);
    return ESP_OK;
}

esp_err_t profile_save(int slot, const print_profile_t *profile)
{
    if (slot < 0 || slot >= PROFILE_SLOT_COUNT) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, slot);

    ret = nvs_set_blob(nvs, key, profile, sizeof(print_profile_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved profile slot %d", slot);
    }
    return ret;
}
