#pragma once

#include "esp_err.h"

/** Start the HTTP web server (REST API + web UI). */
esp_err_t web_server_start(void);

/** Stop the HTTP web server. */
esp_err_t web_server_stop(void);
