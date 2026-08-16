#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* HAL */
#include "hal_gpio.h"
#include "hal_spi_bus.h"
#include "hal_motor.h"
#include "hal_uv_led.h"
#include "hal_backlight.h"
#include "hal_endstop.h"
#include "hal_buttons.h"

/* Display */
#include "tft_driver.h"
#include "touch_input.h"

/* Storage */
#include "sd_card.h"
#include "profile_store.h"

/* Print engine */
#include "print_engine.h"

/* UI */
#include "ui_manager.h"

/* WiFi */
#ifdef CONFIG_LITE3DP_WIFI_ENABLED
#include "wifi_manager.h"
#include "web_server.h"
#include "api_key.h"
#endif

static const char *TAG = "main";

#define INPUT_QUEUE_DEPTH   16

void app_main(void)
{
    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "  Lite3DP Gen 2 Firmware");
    ESP_LOGI(TAG, "  ESP-IDF + LVGL + WiFi");
    ESP_LOGI(TAG, "==================================");

    /* ── Phase 1: Core init ────────────────────────────────────── */

    /* NVS (needed by profiles and WiFi) */
    profile_store_init();

    /* GPIO pins */
    ESP_ERROR_CHECK(hal_gpio_init());

    /* PWM peripherals */
    ESP_ERROR_CHECK(uv_led_init());
    ESP_ERROR_CHECK(backlight_init());

    /* Endstop */
    ESP_ERROR_CHECK(endstop_init());

    /* ── Phase 2: Display init ─────────────────────────────────── */

    /* Initialize the shared SPI bus (TFT, SD, touch share it) */
    ESP_ERROR_CHECK(spi_bus_shared_init());

    ESP_ERROR_CHECK(tft_init());

#ifdef CONFIG_LITE3DP_DISPLAY_TEST
    backlight_set(100);
    tft_test_pattern();
#endif

    tft_fill_screen(0x0000);   /* Clear to black */

    /* Initialize XPT2046 touch controller */
    ESP_ERROR_CHECK(touch_input_init());

    /* ── Phase 3: Storage ──────────────────────────────────────── */

    if (sd_card_present()) {
        esp_err_t sd_ret = sd_card_init();
        if (sd_ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card ready");
        } else {
            ESP_LOGW(TAG, "SD card present but mount failed");
        }
    } else {
        ESP_LOGW(TAG, "No SD card detected");
    }

    /* Load active profile */
    print_profile_t active_profile;
    profile_load(0, &active_profile);
    ESP_LOGI(TAG, "Active profile: layer=%.3fmm expo=%.1fs bottom=%ds",
             (double)active_profile.layer_height,
             (double)active_profile.exposure_time,
             active_profile.bottom_exposure);

    /* ── Phase 4: Print engine ─────────────────────────────────── */

    ESP_ERROR_CHECK(print_engine_init());

    /* ── Phase 5: Input + UI ───────────────────────────────────── */

    QueueHandle_t input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(button_msg_t));
    ESP_ERROR_CHECK(buttons_init(input_queue));
    ESP_ERROR_CHECK(ui_init(input_queue));

    /* Turn on backlight for UI */
    backlight_set(100);

    /* ── Phase 6: WiFi + Web server ────────────────────────────── */

#ifdef CONFIG_LITE3DP_WIFI_ENABLED
    ESP_ERROR_CHECK(wifi_manager_init());

    /* After wifi_manager_init: esp_random() needs the RF subsystem running
     * to be a true RNG, and a predictable first key would defeat the point. */
    ESP_ERROR_CHECK(api_key_init());

    /* Try saved credentials first, fall back to AP */
    char ssid[33] = {0}, pass[65] = {0};
    if (wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Attempting STA connection to: %s", ssid);
        if (wifi_connect_sta(ssid, pass) != ESP_OK) {
            ESP_LOGW(TAG, "STA failed, starting AP mode");
            wifi_start_ap(CONFIG_LITE3DP_DEFAULT_AP_SSID, CONFIG_LITE3DP_DEFAULT_AP_PASS);
        }
    } else {
        ESP_LOGI(TAG, "No saved WiFi credentials, starting AP mode");
        wifi_start_ap(CONFIG_LITE3DP_DEFAULT_AP_SSID, CONFIG_LITE3DP_DEFAULT_AP_PASS);
    }

    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "Web UI at http://%s or http://%s.local",
             wifi_get_ip_str(), CONFIG_LITE3DP_MDNS_HOSTNAME);
#endif

    /* ── Done ──────────────────────────────────────────────────── */

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    /* app_main returns — FreeRTOS tasks continue running */
}
