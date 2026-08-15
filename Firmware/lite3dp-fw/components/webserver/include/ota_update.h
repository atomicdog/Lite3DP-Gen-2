#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/** Register the OTA update HTTP handler on the given server. */
esp_err_t ota_register_handler(httpd_handle_t server);
