/* Profile editing and job preview over HTTP — the web equivalents of the
 * touchscreen's Profile Editor, Settings slots and Print Preview screens,
 * so the printer stays fully operable when touch is not. */

#include "web_profile.h"
#include "web_debug.h"   /* WEB_REGISTER_ROUTES */
#include "api_key.h"
#include "profile_store.h"
#include "print_engine.h"
#include "print_params.h"
#include "slicer_detect.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "web_prof";

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

static cJSON *recv_json(httpd_req_t *req)
{
    char buf[512];
    if (req->content_len == 0 || req->content_len >= sizeof(buf)) return NULL;
    int len = httpd_req_recv(req, buf, req->content_len);
    if (len <= 0) return NULL;
    buf[len] = '\0';
    return cJSON_Parse(buf);
}

static void profile_to_json(const print_profile_t *p, cJSON *item)
{
    cJSON_AddNumberToObject(item, "layerHeight", p->layer_height);
    cJSON_AddNumberToObject(item, "exposureTime", p->exposure_time);
    cJSON_AddNumberToObject(item, "bottomExposure", p->bottom_exposure);
    cJSON_AddNumberToObject(item, "bottomLayers", p->bottom_layers);
    cJSON_AddNumberToObject(item, "transitionLayers", p->transition_layers);
    cJSON_AddNumberToObject(item, "liftHeight", p->lift_height);
    cJSON_AddNumberToObject(item, "liftHeightInitial", p->lift_height_initial);
    cJSON_AddNumberToObject(item, "liftSpeed", p->lift_speed);
    cJSON_AddNumberToObject(item, "liftSpeedInitial", p->lift_speed_initial);
    cJSON_AddNumberToObject(item, "retractSpeed", p->retract_speed);
    cJSON_AddNumberToObject(item, "restTimeMs", p->rest_time_ms);
    cJSON_AddNumberToObject(item, "calibrationOffset", p->calibration_offset);
    cJSON_AddNumberToObject(item, "uvPower", p->uv_power);
}

/* Apply only the keys present, so a client can PATCH a single field
 * without having to echo the whole profile back. */
static void json_to_profile(const cJSON *src, print_profile_t *p)
{
    const cJSON *it;

    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "layerHeight")))
        p->layer_height = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "exposureTime")))
        p->exposure_time = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "bottomExposure")))
        p->bottom_exposure = it->valueint;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "bottomLayers")))
        p->bottom_layers = it->valueint;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "transitionLayers")))
        p->transition_layers = it->valueint;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "liftHeight")))
        p->lift_height = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "liftHeightInitial")))
        p->lift_height_initial = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "liftSpeed")))
        p->lift_speed = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "liftSpeedInitial")))
        p->lift_speed_initial = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "retractSpeed")))
        p->retract_speed = (float)it->valuedouble;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "restTimeMs")))
        p->rest_time_ms = it->valueint;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "calibrationOffset")))
        p->calibration_offset = it->valueint;
    if (cJSON_IsNumber(it = cJSON_GetObjectItem(src, "uvPower")))
        p->uv_power = (uint8_t)it->valueint;
}

/* Reject values that would damage the machine or stall a print. The
 * touchscreen spinboxes enforce ranges; over HTTP nothing else does. */
static const char *profile_validate(const print_profile_t *p)
{
    if (p->layer_height < 0.01f || p->layer_height > 0.2f)
        return "layerHeight must be 0.01-0.2 mm";
    if (p->exposure_time < 0.1f || p->exposure_time > 120.0f)
        return "exposureTime must be 0.1-120 s";
    if (p->bottom_exposure < 1 || p->bottom_exposure > 300)
        return "bottomExposure must be 1-300 s";
    if (p->bottom_layers < 0 || p->bottom_layers > 100)
        return "bottomLayers must be 0-100";
    if (p->transition_layers < 0 || p->transition_layers > 100)
        return "transitionLayers must be 0-100";
    if (p->lift_height < 0.1f || p->lift_height > 50.0f)
        return "liftHeight must be 0.1-50 mm";
    if (p->lift_height_initial < 0.1f || p->lift_height_initial > 50.0f)
        return "liftHeightInitial must be 0.1-50 mm";
    if (p->lift_speed <= 0.0f || p->lift_speed > 10.0f)
        return "liftSpeed must be 0-10 mm/s";
    if (p->lift_speed_initial <= 0.0f || p->lift_speed_initial > 10.0f)
        return "liftSpeedInitial must be 0-10 mm/s";
    if (p->retract_speed <= 0.0f || p->retract_speed > 10.0f)
        return "retractSpeed must be 0-10 mm/s";
    if (p->rest_time_ms < 0 || p->rest_time_ms > 60000)
        return "restTimeMs must be 0-60000";
    if (p->calibration_offset < -100000 || p->calibration_offset > 100000)
        return "calibrationOffset out of range";
    return NULL;
}

/* Which saved slot the active profile came from.
 *
 * Derived by comparison rather than remembered: loading a slot copies its
 * bytes into slot 0 and nothing records the origin, so a stored "current
 * slot" would go stale the moment someone edited a field. Comparing means
 * an edit simply stops matching, which is exactly the "modified" state we
 * want to show. Empty slots are excluded — they read back as defaults and
 * would otherwise match any untouched profile.
 *
 * @return 1-6, or 0 when the active profile matches no saved slot.
 */
static int active_matching_slot(const print_profile_t *active)
{
    for (int slot = 1; slot < PROFILE_SLOT_COUNT; slot++) {
        if (!profile_exists(slot)) continue;

        print_profile_t candidate;
        if (profile_load(slot, &candidate) != ESP_OK) continue;
        if (profile_equal(active, &candidate)) return slot;
    }
    return 0;
}

/* Report the slot match plus which slots hold anything, so the UI can
 * label the active profile and grey out empty slots. */
static void add_slot_info(cJSON *j, const print_profile_t *active)
{
    cJSON_AddNumberToObject(j, "matchingSlot", active_matching_slot(active));

    cJSON *used = cJSON_AddArrayToObject(j, "slotsUsed");
    for (int slot = 1; slot < PROFILE_SLOT_COUNT; slot++) {
        if (profile_exists(slot)) {
            cJSON_AddItemToArray(used, cJSON_CreateNumber(slot));
        }
    }
}

/* ── GET /api/profile — the active profile ─────────────────────── */

static esp_err_t handler_profile_get(httpd_req_t *req)
{
    print_profile_t p;
    profile_load(0, &p);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "slot", 0);
    profile_to_json(&p, j);
    add_slot_info(j, &p);
    return send_json_owned(req, j);
}

/* ── POST /api/profile — edit the active profile ───────────────── */

static esp_err_t handler_profile_post(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;
    if (!print_is_idle()) {
        return reply_error(req, "409 Conflict", "cannot edit the profile mid-print");
    }

    cJSON *body = recv_json(req);
    if (!body) return reply_error(req, "400 Bad Request", "invalid JSON body");

    print_profile_t p;
    profile_load(0, &p);
    json_to_profile(body, &p);
    cJSON_Delete(body);

    const char *err = profile_validate(&p);
    if (err) return reply_error(req, "400 Bad Request", err);

    if (profile_save(0, &p) != ESP_OK) {
        return reply_error(req, "500 Internal Server Error", "could not save profile");
    }

    ESP_LOGI(TAG, "Active profile updated");
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    profile_to_json(&p, j);
    add_slot_info(j, &p);
    return send_json_owned(req, j);
}

/* ── POST /api/profile/slot {slot, action:"load"|"save"} ───────── */

static esp_err_t handler_profile_slot(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;
    if (!print_is_idle()) {
        return reply_error(req, "409 Conflict", "cannot change profiles mid-print");
    }

    cJSON *body = recv_json(req);
    if (!body) return reply_error(req, "400 Bad Request", "invalid JSON body");

    const cJSON *slot_item = cJSON_GetObjectItem(body, "slot");
    const cJSON *act_item  = cJSON_GetObjectItem(body, "action");
    int slot = cJSON_IsNumber(slot_item) ? slot_item->valueint : -1;
    bool load = cJSON_IsString(act_item) && strcmp(act_item->valuestring, "load") == 0;
    bool save = cJSON_IsString(act_item) && strcmp(act_item->valuestring, "save") == 0;
    cJSON_Delete(body);

    if (slot < 1 || slot >= PROFILE_SLOT_COUNT) {
        return reply_error(req, "400 Bad Request", "slot must be 1-6");
    }
    if (!load && !save) {
        return reply_error(req, "400 Bad Request", "action must be \"load\" or \"save\"");
    }

    print_profile_t p;
    esp_err_t ret;
    if (load) {
        /* Slot -> active, mirroring the touch UI's Load button */
        if (profile_load(slot, &p) != ESP_OK) {
            return reply_error(req, "404 Not Found", "slot is empty");
        }
        ret = profile_save(0, &p);
    } else {
        profile_load(0, &p);
        ret = profile_save(slot, &p);
    }

    if (ret != ESP_OK) {
        return reply_error(req, "500 Internal Server Error", "could not write profile");
    }

    ESP_LOGI(TAG, "Profile slot %d %s", slot, load ? "loaded" : "saved");
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    cJSON_AddNumberToObject(j, "slot", slot);
    cJSON_AddStringToObject(j, "action", load ? "load" : "save");
    profile_to_json(&p, j);
    add_slot_info(j, &p);
    return send_json_owned(req, j);
}

/* ── GET /api/job/preview?folder=NAME ──────────────────────────── */
/* What the touchscreen shows before you commit to a print. */

static esp_err_t handler_job_preview(httpd_req_t *req)
{
    char query[128], folder[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "folder", folder, sizeof(folder)) != ESP_OK) {
        return reply_error(req, "400 Bad Request", "missing ?folder=NAME");
    }

    print_job_t job;
    esp_err_t ret = print_job_build(folder, &job);
    if (ret == ESP_ERR_INVALID_ARG) {
        return reply_error(req, "400 Bad Request", "invalid folder name");
    }

    const print_profile_t *p = &job.profile;

    /* Same estimate the touch preview shows */
    int layers = job.total_layers;
    float avg_expo = layers > 0
        ? (p->bottom_layers * (float)p->bottom_exposure +
           (layers - p->bottom_layers) * p->exposure_time) / (float)layers
        : 0.0f;
    float lift_time  = p->lift_height / p->lift_speed +
                       p->lift_height / p->retract_speed;
    float layer_time = avg_expo + lift_time + p->rest_time_ms / 1000.0f;
    int est_seconds  = (int)(layer_time * layers);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", ret == ESP_OK ? "ok" : "empty");
    cJSON_AddStringToObject(j, "folder", job.folder_name);
    cJSON_AddStringToObject(j, "slicer", slicer_type_name(job.slicer));
    cJSON_AddNumberToObject(j, "layers", layers);
    cJSON_AddNumberToObject(j, "estimatedSeconds", est_seconds);
    cJSON *prof = cJSON_AddObjectToObject(j, "profile");
    profile_to_json(p, prof);
    return send_json_owned(req, j);
}

void web_profile_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/profile",      .method = HTTP_GET,  .handler = handler_profile_get },
        { .uri = "/api/profile",      .method = HTTP_POST, .handler = handler_profile_post },
        { .uri = "/api/profile/slot", .method = HTTP_POST, .handler = handler_profile_slot },
        { .uri = "/api/job/preview",  .method = HTTP_GET,  .handler = handler_job_preview },
    };
    WEB_REGISTER_ROUTES(server, routes, TAG);
}
