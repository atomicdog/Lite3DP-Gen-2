#pragma once

#include "esp_http_server.h"

/**
 * Register profile and job endpoints — the web counterparts of the
 * touchscreen's Profile Editor, profile slots and Print Preview:
 *   GET  /api/profile           active profile
 *   POST /api/profile           edit it (partial updates allowed)
 *   POST /api/profile/slot      {slot, action:"load"|"save"}
 *   GET  /api/job/preview?folder=NAME
 */
void web_profile_register(httpd_handle_t server);
