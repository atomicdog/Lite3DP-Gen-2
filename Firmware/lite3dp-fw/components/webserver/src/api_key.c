/* Per-device API key for the mutating HTTP endpoints.
 *
 * Generated on the printer, not at build time, so a shared firmware
 * binary carries no secret. Physical access to the display is the trust
 * anchor: the key is shown on the WiFi Status screen, and the reset
 * button there is the recovery path if it is lost. */

#include "api_key.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "api_key";

#define NVS_NAMESPACE   "lite3dp"
#define NVS_KEY_API     "api_key"

static char s_key[API_KEY_BUFSZ];

static void generate_key(char *out)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t raw[API_KEY_LEN / 2];

    /* esp_random() is only a true RNG with RF running; api_key_init is
     * called after wifi_manager_init for exactly that reason. */
    esp_fill_random(raw, sizeof(raw));

    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2]     = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 0x0F];
    }
    out[API_KEY_LEN] = '\0';
}

static esp_err_t persist_key(const char *key)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(nvs, NVS_KEY_API, key);
    if (ret == ESP_OK) ret = nvs_commit(nvs);
    nvs_close(nvs);
    return ret;
}

esp_err_t api_key_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_key);
        esp_err_t ret = nvs_get_str(nvs, NVS_KEY_API, s_key, &len);
        nvs_close(nvs);
        if (ret == ESP_OK && strlen(s_key) == API_KEY_LEN) {
            ESP_LOGI(TAG, "API key loaded from NVS");
            return ESP_OK;
        }
    }

    return api_key_regenerate();
}

esp_err_t api_key_regenerate(void)
{
    generate_key(s_key);

    esp_err_t ret = persist_key(s_key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save API key: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Logged once so a printer with a dead display can still be paired */
    ESP_LOGW(TAG, "New API key: %s", s_key);
    ESP_LOGW(TAG, "Also shown on the WiFi Status screen.");
    return ESP_OK;
}

const char *api_key_get(void)
{
    return s_key;
}

/* Length-independent compare so a wrong key leaks nothing through timing. */
static bool secure_equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < la && i < lb; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool api_key_check(httpd_req_t *req)
{
    char given[API_KEY_BUFSZ] = {0};

    esp_err_t ret = httpd_req_get_hdr_value_str(req, "X-Api-Key", given, sizeof(given));
    if (ret == ESP_OK && s_key[0] != '\0' && secure_equal(given, s_key)) {
        return true;
    }

    ESP_LOGW(TAG, "Rejected %s (bad or missing X-Api-Key)", req->uri);
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"status\":\"error\",\"message\":\"missing or invalid X-Api-Key; "
        "see the WiFi Status screen on the printer\"}");
    return false;
}
