#pragma once

#include "esp_http_server.h"
#include "esp_log.h"

/**
 * Register routes, complaining loudly if any fail.
 * httpd_register_uri_handler() returns an error once max_uri_handlers is
 * exhausted; ignoring it means the route just 404s at runtime with no
 * clue why, which cost an afternoon once already.
 */
#define WEB_REGISTER_ROUTES(server, routes, tag)                               \
    do {                                                                       \
        for (size_t _i = 0; _i < sizeof(routes) / sizeof((routes)[0]); _i++) { \
            esp_err_t _r = httpd_register_uri_handler((server), &(routes)[_i]);\
            if (_r != ESP_OK) {                                                \
                ESP_LOGE((tag), "Route %s NOT registered: %s",                 \
                         (routes)[_i].uri, esp_err_to_name(_r));               \
            }                                                                  \
        }                                                                      \
    } while (0)

/**
 * Register development endpoints on an already-started server:
 *   GET /api/screenshot   — live LVGL screen as a raw RGB565 area stream
 *   GET /api/ui/nav?screen=N — navigate the UI without the touchscreen
 * See tools/view_screen.py for the client.
 */
void web_debug_register(httpd_handle_t server);
