/* Development aids: pull the live LVGL screen over HTTP and drive
 * navigation without touching the (unreliable) touchscreen.
 * See tools/view_screen.py for the client side. */

#include "web_debug.h"
#include "ui_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "web_dbg";

/* Stream format (little-endian header fields):
 *   "L3DP" magic, u16 width, u16 height
 *   then per flushed area: u16 x1, y1, x2, y2 followed by
 *   (x2-x1+1)*(y2-y1+1) RGB565 pixels, big-endian (as sent to the panel).
 * Areas may arrive in any order and need not cover the screen in one pass. */
#define SCREENSHOT_MAGIC "L3DP"

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static esp_err_t capture_sink(void *ctx, uint16_t x1, uint16_t y1,
                              uint16_t x2, uint16_t y2,
                              const void *pixels, size_t len)
{
    httpd_req_t *req = (httpd_req_t *)ctx;

    uint8_t hdr[8];
    put_u16(&hdr[0], x1);
    put_u16(&hdr[2], y1);
    put_u16(&hdr[4], x2);
    put_u16(&hdr[6], y2);

    if (httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr)) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, (const char *)pixels, len);
}

/* ── GET /api/screenshot ───────────────────────────────────────── */

static esp_err_t handler_screenshot(httpd_req_t *req)
{
    uint16_t w = 0, h = 0;

    /* Geometry first so the client can size its image before pixels arrive */
    esp_err_t err = ui_capture_screen(NULL, NULL, &w, &h);
    if (err == ESP_ERR_INVALID_STATE) {
        /* esp_http_server's error enum has no 409, so set the line directly */
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req,
            "{\"status\":\"error\",\"message\":\"display in use by active print\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/octet-stream");

    uint8_t hdr[8];
    memcpy(hdr, SCREENSHOT_MAGIC, 4);
    put_u16(&hdr[4], w);
    put_u16(&hdr[6], h);
    if (httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr)) != ESP_OK) {
        return ESP_FAIL;
    }

    err = ui_capture_screen(capture_sink, req, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "screenshot aborted: %s", esp_err_to_name(err));
        /* Terminate the chunked stream regardless — a short image is more
         * useful to the client than a hung connection. */
    }

    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "screenshot sent (%ux%u)", w, h);
    return ESP_OK;
}

/* ── GET /api/ui/nav?screen=N ──────────────────────────────────── */

static esp_err_t handler_ui_nav(httpd_req_t *req)
{
    char query[64];
    char val[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "screen", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ?screen=N");
        return ESP_OK;
    }

    int screen = atoi(val);
    if (screen < 0 || screen >= SCREEN_COUNT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "screen out of range");
        return ESP_OK;
    }

    ui_navigate((screen_id_t)screen);

    char resp[64];
    int n = snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"screen\":%d}", screen);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, n);
    ESP_LOGI(TAG, "navigated to screen %d via web", screen);
    return ESP_OK;
}

void web_debug_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/screenshot", .method = HTTP_GET, .handler = handler_screenshot },
        { .uri = "/api/ui/nav",     .method = HTTP_GET, .handler = handler_ui_nav },
    };
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
}
