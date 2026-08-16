/* Manual machine control over HTTP — the touchscreen equivalents of the
 * calibration and utilities screens, usable when touch is unreliable.
 * Every motion/UV command is refused unless the printer is idle. */

#include "web_control.h"
#include "api_key.h"
#include "print_engine.h"
#include "hal_motor.h"
#include "hal_uv_led.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "web_ctl";

/* UV is switched off by a timer so the handler never blocks the server */
static esp_timer_handle_t s_uv_timer;

static esp_err_t send_json_owned(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t reply_error(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "error");
    cJSON_AddStringToObject(j, "message", msg);
    return send_json_owned(req, j);
}

/* Manual control is only safe while no print owns the motor/UV/panel. */
static bool printer_is_idle(void)
{
    print_status_t st;
    print_get_status(&st);
    return st.state == PRINT_STATE_IDLE ||
           st.state == PRINT_STATE_FINISHED ||
           st.state == PRINT_STATE_CANCELLED ||
           st.state == PRINT_STATE_ERROR;
}

/* Read the whole request body into buf and parse it as JSON.
 * An empty body is treated as an empty object. */
static cJSON *recv_json(httpd_req_t *req)
{
    char buf[192];
    if (req->content_len == 0) return cJSON_CreateObject();
    if (req->content_len >= sizeof(buf)) return NULL;

    int len = httpd_req_recv(req, buf, req->content_len);
    if (len <= 0) return NULL;
    buf[len] = '\0';
    return cJSON_Parse(buf);
}

static double json_num(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

/* ── POST /api/motor/jog  {mm: ±float, speed: float} ───────────── */

static esp_err_t handler_motor_jog(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;
    if (!printer_is_idle()) {
        return reply_error(req, "409 Conflict", "printer is busy");
    }

    cJSON *body = recv_json(req);
    if (!body) return reply_error(req, "400 Bad Request", "invalid JSON body");

    double mm    = json_num(body, "mm", 0.0);
    double speed = json_num(body, "speed", 2.0);
    cJSON_Delete(body);

    /* Guard rails: the Z axis has ~150 mm of travel and the endstop only
     * protects the bottom, so cap a single jog well short of the range. */
    if (mm == 0.0 || mm < -50.0 || mm > 50.0) {
        return reply_error(req, "400 Bad Request", "mm must be non-zero, within +/-50");
    }
    if (speed <= 0.0 || speed > 10.0) {
        return reply_error(req, "400 Bad Request", "speed must be within (0, 10]");
    }

    motor_enable();
    esp_err_t ret = motor_move_mm((float)mm, (float)speed);
    if (ret != ESP_OK) {
        return reply_error(req, "500 Internal Server Error", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "jog %.2f mm @ %.2f mm/s", mm, speed);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    cJSON_AddNumberToObject(j, "mm", mm);
    cJSON_AddNumberToObject(j, "speed", speed);
    return send_json_owned(req, j);
}

/* ── POST /api/motor/home ──────────────────────────────────────── */

static esp_err_t handler_motor_home(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;
    if (!printer_is_idle()) {
        return reply_error(req, "409 Conflict", "printer is busy");
    }

    motor_enable();
    esp_err_t ret = motor_home();   /* blocking until the endstop trips */
    if (ret != ESP_OK) {
        return reply_error(req, "500 Internal Server Error", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "homed");
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    return send_json_owned(req, j);
}

/* ── POST /api/motor/off ───────────────────────────────────────── */

static esp_err_t handler_motor_off(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;
    motor_disable();
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    return send_json_owned(req, j);
}

/* ── POST /api/uv  {duty: 0-255, seconds: float} ───────────────── */

static void uv_timeout_cb(void *arg)
{
    uv_led_off();
    ESP_LOGI(TAG, "UV timeout — off");
}

static esp_err_t handler_uv(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;
    if (!printer_is_idle()) {
        return reply_error(req, "409 Conflict", "printer is busy");
    }

    cJSON *body = recv_json(req);
    if (!body) return reply_error(req, "400 Bad Request", "invalid JSON body");

    double duty    = json_num(body, "duty", 0.0);
    double seconds = json_num(body, "seconds", 3.0);
    cJSON_Delete(body);

    if (duty < 0.0 || duty > 255.0) {
        return reply_error(req, "400 Bad Request", "duty must be 0-255");
    }
    /* Bounded so a lost client can't leave the LED curing the vat */
    if (seconds <= 0.0 || seconds > 60.0) {
        return reply_error(req, "400 Bad Request", "seconds must be within (0, 60]");
    }

    if (!s_uv_timer) {
        const esp_timer_create_args_t args = { .callback = uv_timeout_cb, .name = "web_uv" };
        esp_timer_create(&args, &s_uv_timer);
    }
    esp_timer_stop(s_uv_timer);   /* restart the window on a repeat request */

    if (duty == 0.0) {
        uv_led_off();
    } else {
        uv_led_set_power((uint8_t)duty);
        esp_timer_start_once(s_uv_timer, (uint64_t)(seconds * 1000000.0));
    }

    ESP_LOGI(TAG, "UV duty=%d for %.1fs", (int)duty, seconds);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    cJSON_AddNumberToObject(j, "duty", duty);
    cJSON_AddNumberToObject(j, "seconds", seconds);
    return send_json_owned(req, j);
}

void web_control_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/motor/jog",  .method = HTTP_POST, .handler = handler_motor_jog },
        { .uri = "/api/motor/home", .method = HTTP_POST, .handler = handler_motor_home },
        { .uri = "/api/motor/off",  .method = HTTP_POST, .handler = handler_motor_off },
        { .uri = "/api/uv",         .method = HTTP_POST, .handler = handler_uv },
    };
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
}
