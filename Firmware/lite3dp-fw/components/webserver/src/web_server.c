#include "web_server.h"
#include "ota_update.h"
#include "wifi_manager.h"
#include "print_engine.h"
#include "print_params.h"
#include "profile_store.h"
#include "slicer_detect.h"
#include "sd_card.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "web_srv";

static httpd_handle_t s_server = NULL;

/* ── Helper: send JSON response ────────────────────────────────── */

static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/* ── GET /api/status ───────────────────────────────────────────── */

static esp_err_t handler_status(httpd_req_t *req)
{
    print_status_t status;
    print_get_status(&status);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "state", status.state);
    cJSON_AddNumberToObject(j, "currentLayer", status.current_layer);
    cJSON_AddNumberToObject(j, "totalLayers", status.total_layers);
    cJSON_AddNumberToObject(j, "elapsedMs", status.elapsed_ms);
    cJSON_AddNumberToObject(j, "remainingMs", status.estimated_remaining_ms);
    cJSON_AddStringToObject(j, "folder", status.folder_name);

    const char *state_str;
    switch (status.state) {
    case PRINT_STATE_IDLE:        state_str = "idle"; break;
    case PRINT_STATE_CALIBRATING: state_str = "calibrating"; break;
    case PRINT_STATE_PRINTING_BOTTOM:
    case PRINT_STATE_PRINTING_TRANSITION:
    case PRINT_STATE_PRINTING_NORMAL:  state_str = "printing"; break;
    case PRINT_STATE_PAUSED:      state_str = "paused"; break;
    case PRINT_STATE_FINISHING:   state_str = "finishing"; break;
    case PRINT_STATE_FINISHED:    state_str = "finished"; break;
    case PRINT_STATE_ERROR:       state_str = "error"; break;
    case PRINT_STATE_CANCELLED:   state_str = "cancelled"; break;
    default:                      state_str = "unknown"; break;
    }
    cJSON_AddStringToObject(j, "stateStr", state_str);

    return send_json(req, j);
}

/* ── GET /api/files ────────────────────────────────────────────── */

static esp_err_t handler_files(httpd_req_t *req)
{
    sd_entry_t entries[SD_MAX_ENTRIES];
    int count = 0;

    cJSON *j = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(j, "files");

    if (sd_list_dir("", entries, SD_MAX_ENTRIES, &count) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", entries[i].name);
            cJSON_AddBoolToObject(item, "isDir", entries[i].is_dir);
            cJSON_AddItemToArray(arr, item);
        }
    }

    return send_json(req, j);
}

/* ── GET /api/profiles ─────────────────────────────────────────── */

static esp_err_t handler_profiles_get(httpd_req_t *req)
{
    cJSON *j = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(j, "profiles");

    for (int i = 0; i < PROFILE_SLOT_COUNT; i++) {
        print_profile_t p;
        profile_load(i, &p);

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "slot", i);
        cJSON_AddNumberToObject(item, "layerHeight", p.layer_height);
        cJSON_AddNumberToObject(item, "exposureTime", p.exposure_time);
        cJSON_AddNumberToObject(item, "bottomExposure", p.bottom_exposure);
        cJSON_AddNumberToObject(item, "bottomLayers", p.bottom_layers);
        cJSON_AddNumberToObject(item, "transitionLayers", p.transition_layers);
        cJSON_AddNumberToObject(item, "liftHeight", p.lift_height);
        cJSON_AddNumberToObject(item, "liftHeightInitial", p.lift_height_initial);
        cJSON_AddNumberToObject(item, "liftSpeed", p.lift_speed);
        cJSON_AddNumberToObject(item, "liftSpeedInitial", p.lift_speed_initial);
        cJSON_AddNumberToObject(item, "retractSpeed", p.retract_speed);
        cJSON_AddNumberToObject(item, "restTimeMs", p.rest_time_ms);
        cJSON_AddNumberToObject(item, "uvPower", p.uv_power);
        cJSON_AddItemToArray(arr, item);
    }

    return send_json(req, j);
}

/* ── POST /api/print/start ─────────────────────────────────────── */

static esp_err_t handler_print_start(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *folder_item = cJSON_GetObjectItem(body, "folder");
    if (!cJSON_IsString(folder_item)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'folder'");
        return ESP_FAIL;
    }

    const char *folder = folder_item->valuestring;
    ESP_LOGI(TAG, "Print start requested: %s", folder);

    /* Build the print job */
    print_job_t job;
    memset(&job, 0, sizeof(job));
    strncpy(job.folder_name, folder, sizeof(job.folder_name) - 1);
    snprintf(job.folder_path, sizeof(job.folder_path), "%s/%s", SD_MOUNT_POINT, folder);

    /* Detect slicer */
    slicer_detect(job.folder_path, folder, &job.slicer);

    /* Count layers */
    int file_count = 0;
    sd_count_files(folder, ".png", &file_count);
    job.total_layers = file_count;

    /* Load active profile */
    profile_load(0, &job.profile);

    cJSON_Delete(body);

    if (job.total_layers == 0) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "message", "No PNG files found in folder");
        return send_json(req, resp);
    }

    esp_err_t ret = print_start(&job);

    cJSON *resp = cJSON_CreateObject();
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddNumberToObject(resp, "layers", job.total_layers);
        cJSON_AddStringToObject(resp, "slicer", slicer_type_name(job.slicer));
    } else {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "message", "Print already in progress");
    }
    return send_json(req, resp);
}

/* ── POST /api/print/pause ─────────────────────────────────────── */

static esp_err_t handler_print_pause(httpd_req_t *req)
{
    print_pause();
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "paused");
    return send_json(req, j);
}

/* ── POST /api/print/resume ────────────────────────────────────── */

static esp_err_t handler_print_resume(httpd_req_t *req)
{
    print_resume();
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "resumed");
    return send_json(req, j);
}

/* ── POST /api/print/cancel ────────────────────────────────────── */

static esp_err_t handler_print_cancel(httpd_req_t *req)
{
    print_cancel();
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "cancelled");
    return send_json(req, j);
}

/* ── POST /api/upload?path=<dir> ───────────────────────────────── */
/* Receives raw file data with filename in X-Filename header.
 * Creates directory if needed, writes file to SD card. */

static esp_err_t handler_upload(httpd_req_t *req)
{
    /* Get target directory from query param */
    char query[128] = {0};
    char dir_name[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "path", dir_name, sizeof(dir_name));
    }
    if (strlen(dir_name) == 0) {
        strncpy(dir_name, "upload", sizeof(dir_name) - 1);
    }

    /* Get filename from header */
    char filename[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Filename", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Filename header");
        return ESP_FAIL;
    }

    /* Create directory if it doesn't exist */
    char dir_path[SD_MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", SD_MOUNT_POINT, dir_name);
    mkdir(dir_path, 0775);  /* Ignore error if already exists */

    /* Build full file path */
    char file_path[SD_MAX_PATH + 64];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, filename);

    ESP_LOGI(TAG, "Upload: %s (%d bytes)", file_path, req->content_len);

    FILE *f = fopen(file_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create file: %s", file_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    /* Receive and write in chunks */
    char *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int total_written = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, remaining > 4096 ? 4096 : remaining);
        if (recv_len <= 0) {
            ESP_LOGE(TAG, "Upload receive error at %d/%d bytes", total_written, req->content_len);
            free(buf);
            fclose(f);
            unlink(file_path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        size_t written = fwrite(buf, 1, recv_len, f);
        if ((int)written != recv_len) {
            ESP_LOGE(TAG, "Write error: wrote %d of %d", (int)written, recv_len);
            free(buf);
            fclose(f);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        total_written += recv_len;
        remaining -= recv_len;
    }

    free(buf);
    fclose(f);

    ESP_LOGI(TAG, "Upload complete: %s (%d bytes)", file_path, total_written);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "path", file_path);
    cJSON_AddNumberToObject(resp, "size", total_written);
    return send_json(req, resp);
}

/* ── POST /api/wifi/config ─────────────────────────────────────── */

static esp_err_t handler_wifi_config(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(body, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(body, "password");
    if (!cJSON_IsString(ssid_item)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'ssid'");
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *pass = cJSON_IsString(pass_item) ? pass_item->valuestring : "";

    wifi_save_credentials(ssid, pass);
    cJSON_Delete(body);

    ESP_LOGI(TAG, "WiFi credentials saved, restart to connect");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "saved");
    cJSON_AddStringToObject(resp, "message", "Restart to connect to new network");
    return send_json(req, resp);
}

/* ── GET / — Serve web UI ──────────────────────────────────────── */

static const char *WEB_UI_HTML =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Lite3DP Gen 2</title>"
    "<style>"
    "body{font-family:system-ui;background:#1a1a2e;color:#eee;margin:0;padding:20px;max-width:600px;margin:0 auto}"
    "h1{color:#e94560;text-align:center}"
    "h2{margin-top:0;color:#ccc}"
    ".card{background:#16213e;border-radius:12px;padding:20px;margin:15px 0}"
    ".btn{background:#0f3460;color:#eee;border:none;padding:12px 24px;border-radius:8px;"
    "cursor:pointer;font-size:16px;margin:5px}"
    ".btn:hover{background:#e94560}"
    ".btn-danger{background:#c0392b}"
    ".btn-success{background:#27ae60}"
    ".status{font-size:1.2em;text-align:center;padding:10px}"
    ".progress{background:#0f3460;border-radius:8px;height:24px;margin:10px 0}"
    ".progress-bar{background:#e94560;height:100%;border-radius:8px;transition:width 0.3s}"
    "label{display:block;margin:8px 0 4px;color:#888}"
    "input[type=text],input[type=password]{width:100%;padding:8px;border:1px solid #0f3460;"
    "background:#1a1a2e;color:#eee;border-radius:4px;box-sizing:border-box}"
    "#files{list-style:none;padding:0;margin:0}"
    "#files li{padding:12px;border-bottom:1px solid #0f3460;cursor:pointer;display:flex;justify-content:space-between;align-items:center}"
    "#files li:hover{background:#0f3460}"
    ".drop-zone{border:2px dashed #0f3460;border-radius:12px;padding:40px;text-align:center;"
    "color:#888;cursor:pointer;transition:all 0.2s}"
    ".drop-zone.dragover{border-color:#e94560;color:#e94560;background:rgba(233,69,96,0.1)}"
    ".drop-zone input{display:none}"
    ".upload-progress{display:none;margin-top:10px}"
    ".upload-progress.active{display:block}"
    "</style></head><body>"
    "<h1>Lite3DP Gen 2</h1>"
    /* ── Status card ── */
    "<div class='card'>"
    "<h2>Printer Status</h2>"
    "<div class='status' id='state'>Connecting...</div>"
    "<div class='progress'><div class='progress-bar' id='pbar' style='width:0%'></div></div>"
    "<div id='info' style='text-align:center;color:#888'></div>"
    "<div style='text-align:center;margin-top:15px'>"
    "<button class='btn' onclick='printCmd(\"pause\")'>Pause</button>"
    "<button class='btn' onclick='printCmd(\"resume\")'>Resume</button>"
    "<button class='btn btn-danger' onclick='printCmd(\"cancel\")'>Cancel</button>"
    "</div></div>"
    /* ── File browser card ── */
    "<div class='card'>"
    "<h2>Files on SD Card</h2>"
    "<ul id='files'><li style='color:#888'>Loading...</li></ul>"
    "</div>"
    /* ── Upload card ── */
    "<div class='card'>"
    "<h2>Upload Files</h2>"
    "<label>Target folder</label>"
    "<input type='text' id='upload-dir' placeholder='e.g. MyModel' value=''>"
    "<div class='drop-zone' id='dropzone'>"
    "Drop PNG files here or click to browse"
    "<input type='file' id='fileinput' multiple accept='.png'>"
    "</div>"
    "<div class='upload-progress' id='upprog'>"
    "<div class='progress'><div class='progress-bar' id='upbar' style='width:0%'></div></div>"
    "<div id='upstatus' style='text-align:center;color:#888'></div>"
    "</div></div>"
    /* ── WiFi card ── */
    "<div class='card'>"
    "<h2>WiFi Settings</h2>"
    "<label>SSID</label><input type='text' id='wssid'>"
    "<label>Password</label><input type='password' id='wpass'>"
    "<br><button class='btn' onclick='saveWifi()' style='margin-top:10px'>Save & Restart</button>"
    "</div>"
    /* ── OTA card ── */
    "<div class='card'>"
    "<h2>Firmware Update</h2>"
    "<input type='file' id='otafile' accept='.bin'>"
    "<button class='btn btn-success' onclick='doOta()' style='margin-top:10px'>Upload Firmware</button>"
    "<div id='otastatus' style='margin-top:10px;color:#888'></div>"
    "</div>"
    /* ── JavaScript ── */
    "<script>"
    "async function api(m,u,b){const r=await fetch(u,{method:m,headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):undefined});return r.json()}"
    "function printCmd(c){api('POST','/api/print/'+c)}"
    /* Status polling */
    "async function poll(){"
    "try{const s=await api('GET','/api/status');"
    "document.getElementById('state').textContent=s.stateStr.toUpperCase();"
    "const pct=s.totalLayers>0?Math.round(s.currentLayer/s.totalLayers*100):0;"
    "document.getElementById('pbar').style.width=pct+'%';"
    "const el=Math.floor(s.elapsedMs/1000),rem=Math.floor(s.remainingMs/1000);"
    "const fmt=s=>{const m=Math.floor(s/60);return m+'m'+(s%60)+'s'};"
    "document.getElementById('info').innerHTML="
    "'Layer '+s.currentLayer+'/'+s.totalLayers+' &bull; '+fmt(el)+' elapsed &bull; ETA '+fmt(rem);"
    "}catch(e){document.getElementById('state').textContent='OFFLINE'}}"
    /* File listing */
    "async function loadFiles(){"
    "try{const d=await api('GET','/api/files');"
    "const ul=document.getElementById('files');ul.innerHTML='';"
    "d.files.filter(f=>f.isDir).forEach(f=>{"
    "const li=document.createElement('li');"
    "li.innerHTML='<span>&#128193; '+f.name+'</span><button class=\"btn\" style=\"padding:6px 12px;font-size:13px\">Print</button>';"
    "li.querySelector('button').onclick=e=>{e.stopPropagation();api('POST','/api/print/start',{folder:f.name}).then(r=>{if(r.status==='ok')alert('Print started: '+r.layers+' layers ('+r.slicer+')');else alert('Error: '+(r.message||'unknown'))})};"
    "ul.appendChild(li)});"
    "if(d.files.filter(f=>f.isDir).length===0)ul.innerHTML='<li style=\"color:#888\">No folders found</li>';"
    "}catch(e){document.getElementById('files').innerHTML='<li style=\"color:#e94560\">SD card not available</li>'}}"
    /* File upload with drag-and-drop */
    "const dz=document.getElementById('dropzone'),fi=document.getElementById('fileinput');"
    "dz.onclick=()=>fi.click();"
    "dz.ondragover=e=>{e.preventDefault();dz.classList.add('dragover')};"
    "dz.ondragleave=()=>dz.classList.remove('dragover');"
    "dz.ondrop=e=>{e.preventDefault();dz.classList.remove('dragover');uploadFiles(e.dataTransfer.files)};"
    "fi.onchange=()=>uploadFiles(fi.files);"
    "async function uploadFiles(files){"
    "const dir=document.getElementById('upload-dir').value||'upload';"
    "const prog=document.getElementById('upprog');prog.classList.add('active');"
    "const bar=document.getElementById('upbar'),stat=document.getElementById('upstatus');"
    "let done=0;"
    "for(const f of files){"
    "stat.textContent='Uploading '+f.name+' ('+(done+1)+'/'+files.length+')';"
    "bar.style.width=Math.round(done/files.length*100)+'%';"
    "try{const r=await fetch('/api/upload?path='+encodeURIComponent(dir),"
    "{method:'POST',headers:{'X-Filename':f.name,'Content-Type':'application/octet-stream'},body:f});"
    "if(!r.ok)throw new Error(await r.text());"
    "done++}catch(e){stat.textContent='Error: '+e.message;return}}"
    "bar.style.width='100%';stat.textContent='Done! '+done+' files uploaded to /'+dir;"
    "loadFiles()}"
    /* WiFi config */
    "async function saveWifi(){"
    "const s=document.getElementById('wssid').value,p=document.getElementById('wpass').value;"
    "if(!s){alert('Enter SSID');return}"
    "const r=await api('POST','/api/wifi/config',{ssid:s,password:p});"
    "alert(r.message||'Saved')}"
    /* OTA update */
    "async function doOta(){"
    "const f=document.getElementById('otafile').files[0];"
    "if(!f){alert('Select firmware .bin file');return}"
    "const st=document.getElementById('otastatus');"
    "st.textContent='Uploading firmware ('+Math.round(f.size/1024)+'KB)...';"
    "try{const r=await fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});"
    "const j=await r.json();st.textContent=j.message||'Update complete!';"
    "}catch(e){st.textContent='Error: '+e.message}}"
    /* Init */
    "setInterval(poll,2000);poll();loadFiles();"
    "</script></body></html>";

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, WEB_UI_HTML);
}

/* ── CORS preflight handler ────────────────────────────────────── */

static esp_err_t handler_options(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,PUT,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── Server start/stop ─────────────────────────────────────────── */

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.core_id = 0;  /* Run on Core 0 (same as WiFi) */

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register URI handlers */
    const httpd_uri_t routes[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = handler_root },
        { .uri = "/api/status",      .method = HTTP_GET,  .handler = handler_status },
        { .uri = "/api/files",       .method = HTTP_GET,  .handler = handler_files },
        { .uri = "/api/profiles",    .method = HTTP_GET,  .handler = handler_profiles_get },
        { .uri = "/api/print/start", .method = HTTP_POST, .handler = handler_print_start },
        { .uri = "/api/print/pause", .method = HTTP_POST, .handler = handler_print_pause },
        { .uri = "/api/print/resume",.method = HTTP_POST, .handler = handler_print_resume },
        { .uri = "/api/print/cancel",.method = HTTP_POST, .handler = handler_print_cancel },
        { .uri = "/api/upload",      .method = HTTP_POST, .handler = handler_upload },
        { .uri = "/api/wifi/config", .method = HTTP_POST, .handler = handler_wifi_config },
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    /* Register OTA handler if enabled */
#ifdef CONFIG_LITE3DP_OTA_ENABLED
    ota_register_handler(s_server);
#endif

    ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}
