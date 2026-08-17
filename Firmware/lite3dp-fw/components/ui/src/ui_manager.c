#include "ui_manager.h"
#include "ui_screens.h"
#include "input_handler.h"
#include "print_engine.h"
#include "tft_driver.h"
#include "hal_backlight.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "lvgl.h"

static const char *TAG = "ui";

#define UI_TASK_STACK   12288
#define UI_TASK_PRIO    3
#define UI_TASK_CORE    1
#define UI_TICK_MS      5       /* LVGL tick period */
#define UI_REFRESH_MS   33      /* ~30 Hz screen refresh */

/* Menu runs landscape (rotation 3): 480 wide x 320 tall */
#define UI_MENU_ROTATION    3

/* LVGL draw buffer — two partial buffers for DMA ping-pong.
 * 10 lines at 480 wide costs 19 KB of static RAM for the pair; going wider
 * starves the WiFi/HTTP heap (only ~30 KB free after boot on this board). */
#define LV_BUF_LINES   10
static lv_color_t s_buf1[TFT_NATIVE_LONG_SIDE * LV_BUF_LINES];
static lv_color_t s_buf2[TFT_NATIVE_LONG_SIDE * LV_BUF_LINES];
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static screen_id_t s_current_screen = SCREEN_MAIN_MENU;
static bool s_suspended = false;
static TaskHandle_t s_task_handle;

/* LVGL is not thread-safe: this recursive mutex guards every LVGL call
 * (UI task, print monitor, web handlers) and the s_suspended flag. */
static SemaphoreHandle_t s_lvgl_mutex;

void ui_lock(void)
{
    xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
}

void ui_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

/* Active screen-capture sink (see ui_capture_screen). Only ever set while
 * the LVGL lock is held by the capturing task. */
static ui_capture_cb_t s_capture_cb;
static void *s_capture_ctx;
static esp_err_t s_capture_err;

/* ── LVGL display flush callback ───────────────────────────────── */

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    /* Atomic window+pixels against SD/touch on the shared SPI bus */
    tft_blit(area->x1, area->y1, area->x2, area->y2, (const uint16_t *)color_p);

    if (s_capture_cb && s_capture_err == ESP_OK) {
        s_capture_err = s_capture_cb(s_capture_ctx, area->x1, area->y1,
                                     area->x2, area->y2, color_p,
                                     (size_t)w * h * sizeof(lv_color_t));
    }

    lv_disp_flush_ready(drv);
}

/* ── LVGL tick callback ────────────────────────────────────────── */

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(UI_TICK_MS);
}

/* ── Print event monitor task ──────────────────────────────────── */
/* Runs in background, watches for print completion events and
 * transitions the UI back from the PRINTING screen. */

#define MONITOR_TASK_STACK  3072
#define MONITOR_TASK_PRIO   2

static void print_monitor_task(void *arg)
{
    EventGroupHandle_t eg = print_get_event_group();
    const EventBits_t watch_bits = PRINT_EVT_STARTED | PRINT_EVT_FINISHED |
                                   PRINT_EVT_CANCELLED | PRINT_EVT_ERROR;

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(eg, watch_bits, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & PRINT_EVT_STARTED) {
            /* Print began (touch or web) — hand the panel to the mask */
            ui_lock();
            if (s_current_screen != SCREEN_PRINTING) {
                ui_navigate(SCREEN_PRINTING);
                ESP_LOGI(TAG, "Print started — UI suspended for mask");
            }
            ui_unlock();
        }

        if (!(bits & (PRINT_EVT_FINISHED | PRINT_EVT_CANCELLED | PRINT_EVT_ERROR))) {
            continue;
        }

        ui_lock();
        if (s_current_screen != SCREEN_PRINTING) {
            ui_unlock();
            continue;
        }

        /* Print ended — turn off backlight and restore LVGL */
        backlight_set(0);
        tft_set_rotation(UI_MENU_ROTATION);
        ui_resume();

        if (bits & PRINT_EVT_FINISHED) {
            print_status_t st;
            print_get_status(&st);
            ui_screen_print_done_set_results(st.total_layers, st.elapsed_ms / 1000);
            ui_navigate(SCREEN_PRINT_DONE);
            ESP_LOGI(TAG, "Print finished — showing results");
        } else if (bits & PRINT_EVT_CANCELLED) {
            ui_navigate(SCREEN_MAIN_MENU);
            ESP_LOGI(TAG, "Print cancelled — returning to menu");
        } else {
            ui_navigate(SCREEN_MAIN_MENU);
            ESP_LOGW(TAG, "Print error — returning to menu");
        }
        ui_unlock();
    }
}

/* ── UI task ───────────────────────────────────────────────────── */

static void ui_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        ui_lock();
        if (!s_suspended) {
            lv_timer_handler();
        }
        ui_unlock();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

/* ── Public API ────────────────────────────────────────────────── */

esp_err_t ui_init(QueueHandle_t input_queue)
{
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    /* Menu orientation must be set before LVGL learns the resolution */
    tft_set_rotation(UI_MENU_ROTATION);

    /* Initialize LVGL */
    lv_init();

    /* Setup draw buffers */
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, TFT_NATIVE_LONG_SIDE * LV_BUF_LINES);

    /* Register display driver at the logical (landscape) resolution */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = tft_width();
    s_disp_drv.ver_res = tft_height();
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    /* Register input device */
    input_handler_register(input_queue);

    /* Create LVGL tick timer */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&tick_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, UI_TICK_MS * 1000);

    /* Apply dark theme */
    lv_theme_t *theme = lv_theme_default_init(
        lv_disp_get_default(),
        lv_palette_main(LV_PALETTE_BLUE),       /* Primary color */
        lv_palette_main(LV_PALETTE_DEEP_ORANGE), /* Secondary color */
        true,  /* Dark mode */
        LV_FONT_DEFAULT
    );
    lv_disp_set_theme(lv_disp_get_default(), theme);

    /* Load the main menu screen */
    lv_scr_load(ui_screen_main_menu());

    /* Start UI task */
    BaseType_t ret = xTaskCreatePinnedToCore(
        ui_task, "ui", UI_TASK_STACK, NULL,
        UI_TASK_PRIO, &s_task_handle, UI_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }

    /* Start print event monitor task */
    xTaskCreatePinnedToCore(
        print_monitor_task, "print_mon", MONITOR_TASK_STACK, NULL,
        MONITOR_TASK_PRIO, NULL, UI_TASK_CORE
    );

    ESP_LOGI(TAG, "LVGL UI initialized (%dx%d, buf=%d lines)",
             tft_width(), tft_height(), LV_BUF_LINES);
    return ESP_OK;
}

void ui_navigate(screen_id_t screen)
{
    lv_obj_t *scr = NULL;

    ui_lock();
    switch (screen) {
    case SCREEN_MAIN_MENU:      scr = ui_screen_main_menu(); break;
    case SCREEN_FILE_BROWSER:   scr = ui_screen_file_browser(); break;
    case SCREEN_PRINT_PREVIEW:  scr = ui_screen_print_preview(); break;
    case SCREEN_SETTINGS:       scr = ui_screen_settings(); break;
    case SCREEN_PROFILE_EDITOR: scr = ui_screen_profile_editor(); break;
    case SCREEN_CALIBRATION:    scr = ui_screen_calibration(); break;
    case SCREEN_UTILITIES:      scr = ui_screen_utilities(); break;
    case SCREEN_PRINT_DONE:     scr = ui_screen_print_done(); break;
    case SCREEN_WIFI_STATUS:    scr = ui_screen_wifi_status(); break;
    case SCREEN_TOUCH_TEST:     scr = ui_screen_touch_test(); break;
    case SCREEN_PRINTING:
        /* Printing screen doesn't use LVGL — suspend and use TFT directly */
        ui_suspend();
        s_current_screen = SCREEN_PRINTING;
        ui_unlock();
        return;
    default:
        ESP_LOGW(TAG, "Unknown screen: %d", screen);
        ui_unlock();
        return;
    }

    if (scr) {
        /* Use simple screen load without animation to avoid null pointer
         * crashes in LVGL's transform_point during screen transitions */
        lv_scr_load(scr);
        s_current_screen = screen;
        ESP_LOGI(TAG, "Navigated to screen %d", screen);
    }
    ui_unlock();
}

screen_id_t ui_get_current_screen(void)
{
    return s_current_screen;
}

void ui_suspend(void)
{
    /* Taking the lock guarantees no LVGL flush is mid-flight when the
     * print engine starts pushing mask pixels. */
    ui_lock();
    s_suspended = true;
    ui_unlock();
    ESP_LOGI(TAG, "LVGL suspended (TFT used for print mask)");
}

esp_err_t ui_capture_screen(ui_capture_cb_t cb, void *ctx,
                            uint16_t *out_w, uint16_t *out_h)
{
    ui_lock();

    if (s_suspended) {
        /* Panel belongs to the print mask right now — nothing to capture */
        ui_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (out_w) *out_w = tft_width();
    if (out_h) *out_h = tft_height();

    if (!cb) {
        /* Geometry query only — no redraw */
        ui_unlock();
        return ESP_OK;
    }

    s_capture_cb  = cb;
    s_capture_ctx = ctx;
    s_capture_err = ESP_OK;

    /* Redraw everything so the sink sees the whole screen, not just the
     * areas that happened to change. */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    esp_err_t err = s_capture_err;
    s_capture_cb  = NULL;
    s_capture_ctx = NULL;

    ui_unlock();
    return err;
}

void ui_resume(void)
{
    ui_lock();
    s_suspended = false;
    /* Force full screen redraw */
    lv_obj_invalidate(lv_scr_act());
    ui_unlock();
    ESP_LOGI(TAG, "LVGL resumed");
}
