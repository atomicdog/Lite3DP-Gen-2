#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

/* 16 random bytes, hex-encoded */
#define API_KEY_LEN     32
#define API_KEY_BUFSZ   (API_KEY_LEN + 1)

/**
 * Load this printer's API key from NVS, generating one on first boot.
 *
 * The key is per-device and never compiled in, so the same firmware
 * binary can be shared: every printer mints its own key and shows it on
 * the WiFi Status screen. Call AFTER wifi_manager_init() — esp_random()
 * is only truly random once the RF subsystem is running.
 */
esp_err_t api_key_init(void);

/** The current key (empty string before api_key_init succeeds). */
const char *api_key_get(void);

/** Generate and persist a fresh key, replacing any existing one. */
esp_err_t api_key_regenerate(void);

/**
 * Verify the X-Api-Key header on a request. Returns true when it matches.
 * On failure it sends a 401 response, so the caller should simply return.
 */
bool api_key_check(httpd_req_t *req);
