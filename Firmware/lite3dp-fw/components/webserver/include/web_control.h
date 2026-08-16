#pragma once

#include "esp_http_server.h"

/**
 * Register manual machine-control endpoints on an already-started server:
 *   POST /api/motor/jog   {mm, speed}
 *   POST /api/motor/home
 *   POST /api/motor/off
 *   POST /api/uv          {duty, seconds}
 * All refuse with 409 unless the print engine is idle.
 */
void web_control_register(httpd_handle_t server);
