/* Development aids: pull the live LVGL screen over HTTP and drive
 * navigation without touching the (unreliable) touchscreen.
 * See tools/view_screen.py for the client side. */

#include "web_debug.h"
#include "api_key.h"
#include "ui_manager.h"
#include "ui_screens.h"
#include "touch_input.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
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
    /* Authenticated even though it only reads: the WiFi Status screen
     * displays the access key, so an open screenshot would hand it out. */
    if (!api_key_check(req)) return ESP_OK;

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
    if (!api_key_check(req)) return ESP_OK;

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

/* ── Touch bring-up ────────────────────────────────────────────── */
/* T_IRQ (IO35) has no pull-up anywhere — not internally (input-only pin) and
 * not on the board (schematic sheet 2/2, FPC2 pin 11).  These endpoints exist
 * so touch can be characterised with real numbers instead of by reflashing
 * calibration guesses.  See the touch test plan. */

static esp_err_t send_json_obj(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    return ESP_OK;
}

/* Reads an unsigned query param; leaves *dst alone when absent. */
static void query_u16(const char *query, const char *key, uint16_t *dst)
{
    char val[16];
    if (httpd_query_key_value(query, key, val, sizeof(val)) == ESP_OK) {
        *dst = (uint16_t)atoi(val);
    }
}

static void query_bool(const char *query, const char *key, bool *dst)
{
    char val[16];
    if (httpd_query_key_value(query, key, val, sizeof(val)) == ESP_OK) {
        *dst = (atoi(val) != 0);
    }
}

static cJSON *cal_to_json(const touch_cal_t *c)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "xmin", c->x_min);
    cJSON_AddNumberToObject(j, "xmax", c->x_max);
    cJSON_AddNumberToObject(j, "ymin", c->y_min);
    cJSON_AddNumberToObject(j, "ymax", c->y_max);
    cJSON_AddNumberToObject(j, "w", c->screen_w);
    cJSON_AddNumberToObject(j, "h", c->screen_h);
    cJSON_AddBoolToObject(j, "swap", c->swap_xy);
    cJSON_AddBoolToObject(j, "invx", c->invert_x);
    cJSON_AddBoolToObject(j, "invy", c->invert_y);
    cJSON_AddNumberToObject(j, "pmin", c->pressure_min);
    return j;
}

/* ── GET /api/touch/raw?n=N ────────────────────────────────────── */

static esp_err_t handler_touch_raw(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;

    uint16_t n = 5;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        query_u16(query, "n", &n);
    }

    touch_raw_t r;
    esp_err_t err = touch_input_read_raw(&r, n);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "touch read failed");
        return ESP_OK;
    }

    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "irq", r.irq);
    cJSON_AddNumberToObject(j, "samples", r.samples);
    cJSON_AddNumberToObject(j, "x", r.x_med);
    cJSON_AddNumberToObject(j, "x_min", r.x_min);
    cJSON_AddNumberToObject(j, "x_max", r.x_max);
    cJSON_AddNumberToObject(j, "x_spread", r.x_max - r.x_min);
    cJSON_AddNumberToObject(j, "y", r.y_med);
    cJSON_AddNumberToObject(j, "y_min", r.y_min);
    cJSON_AddNumberToObject(j, "y_max", r.y_max);
    cJSON_AddNumberToObject(j, "y_spread", r.y_max - r.y_min);
    cJSON_AddNumberToObject(j, "z1", r.z1);
    cJSON_AddNumberToObject(j, "z2", r.z2);
    cJSON_AddNumberToObject(j, "pressure", r.pressure);
    cJSON_AddNumberToObject(j, "mapped_x", r.mapped_x);
    cJSON_AddNumberToObject(j, "mapped_y", r.mapped_y);
    return send_json_obj(req, j);
}

/* ── GET/POST /api/touch/cal ───────────────────────────────────── */
/* GET returns the live calibration plus any points captured by the crosshair
 * screen.  POST takes the same field names as query params and applies them,
 * so a mapping can be tried without a flash cycle. */

static esp_err_t handler_touch_cal_get(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;

    touch_cal_t c;
    touch_input_get_cal(&c);

    cJSON *j = cal_to_json(&c);

    const touch_cal_point_t *pts;
    int n = ui_touch_cal_points(&pts);
    cJSON *arr = cJSON_AddArrayToObject(j, "points");
    for (int i = 0; i < n; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "target_x", pts[i].target_x);
        cJSON_AddNumberToObject(p, "target_y", pts[i].target_y);
        cJSON_AddNumberToObject(p, "raw_x", pts[i].raw_x);
        cJSON_AddNumberToObject(p, "raw_y", pts[i].raw_y);
        cJSON_AddNumberToObject(p, "pressure", pts[i].pressure);
        cJSON_AddItemToArray(arr, p);
    }
    return send_json_obj(req, j);
}

static esp_err_t handler_touch_cal_post(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;

    touch_cal_t c;
    touch_input_get_cal(&c);

    char query[192];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        /* ?start=1 opens the crosshair capture instead of setting values */
        bool start = false;
        query_bool(query, "start", &start);
        if (start) {
            ui_navigate(SCREEN_TOUCH_TEST);
            ui_lock();
            ui_touch_cal_start();
            ui_unlock();
            cJSON *j = cJSON_CreateObject();
            cJSON_AddStringToObject(j, "status", "ok");
            cJSON_AddStringToObject(j, "message", "tap each crosshair, then GET /api/touch/cal");
            return send_json_obj(req, j);
        }

        query_u16(query, "xmin", &c.x_min);
        query_u16(query, "xmax", &c.x_max);
        query_u16(query, "ymin", &c.y_min);
        query_u16(query, "ymax", &c.y_max);
        query_u16(query, "w",    &c.screen_w);
        query_u16(query, "h",    &c.screen_h);
        query_u16(query, "pmin", &c.pressure_min);
        query_bool(query, "swap", &c.swap_xy);
        query_bool(query, "invx", &c.invert_x);
        query_bool(query, "invy", &c.invert_y);
    }

    touch_input_set_cal(&c);
    return send_json_obj(req, cal_to_json(&c));
}

void web_debug_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/touch/raw",  .method = HTTP_GET,  .handler = handler_touch_raw },
        { .uri = "/api/touch/cal",  .method = HTTP_GET,  .handler = handler_touch_cal_get },
        { .uri = "/api/touch/cal",  .method = HTTP_POST, .handler = handler_touch_cal_post },
        { .uri = "/api/screenshot", .method = HTTP_GET, .handler = handler_screenshot },
        { .uri = "/api/ui/nav",     .method = HTTP_GET, .handler = handler_ui_nav },
    };
    WEB_REGISTER_ROUTES(server, routes, TAG);
}
