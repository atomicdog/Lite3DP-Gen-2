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

/** Save WiFi credentials to NVS. */
esp_err_t wifi_save_credentials(const char *ssid, const char *password);

/** Load saved WiFi credentials. Returns false if none saved. */
bool wifi_load_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len);
