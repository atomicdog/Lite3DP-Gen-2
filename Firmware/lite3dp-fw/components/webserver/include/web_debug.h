#pragma once

#include "esp_http_server.h"

/**
 * Register development endpoints on an already-started server:
 *   GET /api/screenshot   — live LVGL screen as a raw RGB565 area stream
 *   GET /api/ui/nav?screen=N — navigate the UI without the touchscreen
 * See tools/view_screen.py for the client.
 */
void web_debug_register(httpd_handle_t server);
