#include "ota_update.h"
#include "api_key.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ota";

#define OTA_BUF_SIZE    4096
#define OTA_BUF_MIN     512

static esp_err_t handler_ota(httpd_req_t *req)
{
    if (!api_key_check(req)) return ESP_OK;

    ESP_LOGI(TAG, "OTA update starting (size=%d)", req->content_len);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    /* OTA_SIZE_UNKNOWN erases all 1.6 MB of the partition in one blocking
     * call. Measured at ~5.9 s on this board, which is long enough to trip
     * the TG1 watchdog (rst:0x8) and reboot mid-upload — the OTA could
     * never finish. OTA_WITH_SEQUENTIAL_WRITES erases a sector at a time
     * from inside esp_ota_write() instead, so nothing blocks for long and
     * the erase current is spread across the transfer rather than drawn in
     * one burst. */
    esp_err_t ret = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    /* Heap is scarce on this board and OTA is the primary way to flash it,
     * so settle for a smaller chunk rather than failing outright. */
    size_t buf_size = OTA_BUF_SIZE;
    char *buf = NULL;
    while (!buf && buf_size >= OTA_BUF_MIN) {
        buf = malloc(buf_size);
        if (!buf) buf_size /= 2;
    }
    if (!buf) {
        ESP_LOGE(TAG, "No memory for OTA buffer (free heap %lu)",
                 (unsigned long)esp_get_free_heap_size());
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA buffer %u bytes, free heap %lu",
             (unsigned)buf_size, (unsigned long)esp_get_free_heap_size());

    int total_read = 0;
    int remaining = req->content_len;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf,
            remaining > (int)buf_size ? (int)buf_size : remaining);
        if (recv_len <= 0) {
            ESP_LOGE(TAG, "OTA receive error");
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        ret = esp_ota_write(ota_handle, buf, recv_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(ret));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        total_read += recv_len;
        remaining -= recv_len;
        ESP_LOGD(TAG, "OTA progress: %d/%d bytes", total_read, req->content_len);
    }

    free(buf);

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation failed");
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(update_part);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update complete (%d bytes). Restarting...", total_read);
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Update complete, restarting...\"}");

    /* Restart after a short delay to let the response send */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;  /* unreachable */
}

esp_err_t ota_register_handler(httpd_handle_t server)
{
    const httpd_uri_t ota_uri = {
        .uri     = "/api/ota",
        .method  = HTTP_POST,
        .handler = handler_ota,
    };
    return httpd_register_uri_handler(server, &ota_uri);
}
