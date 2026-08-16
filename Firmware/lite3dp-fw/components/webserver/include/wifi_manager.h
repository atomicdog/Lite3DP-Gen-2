#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_AP_ACTIVE,
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_STA_DISCONNECTED,
} wifi_state_t;

/** Initialize WiFi subsystem. */
esp_err_t wifi_manager_init(void);

/** Start in AP mode (fallback / first boot). */
esp_err_t wifi_start_ap(const char *ssid, const char *password);

/** Connect to a WiFi network in STA mode. */
esp_err_t wifi_connect_sta(const char *ssid, const char *password);

/** Get current WiFi state. */
wifi_state_t wifi_get_state(void);

/** Get the current IP address as a string. */
const char *wifi_get_ip_str(void);

typedef enum {
    WIFI_APPLY_IDLE,
    WIFI_APPLY_IN_PROGRESS,
    WIFI_APPLY_OK,
    WIFI_APPLY_ROLLED_BACK,
} wifi_apply_state_t;

/**
 * Switch to new credentials without rebooting, rolling back if they fail.
 *
 * Returns immediately; the work happens on a short-lived task so the HTTP
 * response reaches the client before the radio changes underneath it.
 * The new credentials are written to NVS *only* once a connection
 * succeeds, so a typo can never strand the printer across a reboot. On
 * failure it reconnects with the previously saved credentials, or falls
 * back to AP mode if there are none.
 *
 * @return ESP_ERR_INVALID_STATE if an apply is already running.
 */
esp_err_t wifi_apply_credentials(const char *ssid, const char *password);

/** Outcome of the last wifi_apply_credentials() call. */
wifi_apply_state_t wifi_get_apply_state(void);

/** Save WiFi credentials to NVS. */
esp_err_t wifi_save_credentials(const char *ssid, const char *password);

/** Load saved WiFi credentials. Returns false if none saved. */
bool wifi_load_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len);
