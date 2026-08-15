#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";

#define NVS_NAMESPACE       "lite3dp"
#define NVS_KEY_WIFI_SSID   "wifi_ssid"
#define NVS_KEY_WIFI_PASS   "wifi_pass"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY           5

static EventGroupHandle_t s_wifi_events;
static wifi_state_t s_state = WIFI_STATE_IDLE;
static char s_ip_str[16] = "0.0.0.0";
static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            s_state = WIFI_STATE_STA_CONNECTING;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_state = WIFI_STATE_STA_DISCONNECTED;
            if (s_retry_count < MAX_RETRY) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_count, MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
                ESP_LOGW(TAG, "Failed to connect after %d retries", MAX_RETRY);
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)data;
            ESP_LOGI(TAG, "Station connected (AID=%d)", evt->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)data;
            ESP_LOGI(TAG, "Station disconnected (AID=%d)", evt->aid);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "Connected! IP: %s", s_ip_str);
        s_state = WIFI_STATE_STA_CONNECTED;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create default network interfaces */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* Initialize mDNS */
    esp_err_t ret = mdns_init();
    if (ret == ESP_OK) {
        mdns_hostname_set(CONFIG_LITE3DP_MDNS_HOSTNAME);
        mdns_instance_name_set("Lite3DP Gen 2 MSLA Printer");
        ESP_LOGI(TAG, "mDNS hostname: %s.local", CONFIG_LITE3DP_MDNS_HOSTNAME);
    }

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_start_ap(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.ap.ssid, ssid, sizeof(wifi_cfg.ap.ssid) - 1);
    wifi_cfg.ap.ssid_len = strlen(ssid);
    wifi_cfg.ap.max_connection = 4;

    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_cfg.ap.password, password, sizeof(wifi_cfg.ap.password) - 1);
        wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_state = WIFI_STATE_AP_ACTIVE;
    strncpy(s_ip_str, "192.168.4.1", sizeof(s_ip_str));

    ESP_LOGI(TAG, "AP started: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t wifi_connect_sta(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retry_count = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    /* Wait for connection or failure */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "STA connection failed, falling back to AP mode");
    return ESP_FAIL;
}

wifi_state_t wifi_get_state(void)
{
    return s_state;
}

const char *wifi_get_ip_str(void)
{
    return s_ip_str;
}

esp_err_t wifi_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    nvs_set_str(nvs, NVS_KEY_WIFI_SSID, ssid);
    nvs_set_str(nvs, NVS_KEY_WIFI_PASS, password);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "WiFi credentials saved");
    return ESP_OK;
}

bool wifi_load_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;

    esp_err_t ret1 = nvs_get_str(nvs, NVS_KEY_WIFI_SSID, ssid, &ssid_len);
    esp_err_t ret2 = nvs_get_str(nvs, NVS_KEY_WIFI_PASS, password, &pass_len);
    nvs_close(nvs);

    return (ret1 == ESP_OK && ret2 == ESP_OK && strlen(ssid) > 0);
}
