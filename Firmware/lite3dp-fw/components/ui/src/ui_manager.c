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
#include "freertos/event_groups.h"
#include "lvgl.h"

static const char *TAG = "ui";

#define UI_TASK_STACK   12288
#define UI_TASK_PRIO    3
#define UI_TASK_CORE    1
#define UI_TICK_MS      5       /* LVGL tick period */
#define UI_REFRESH_MS   33      /* ~30 Hz screen refresh */

/* LVGL draw buffer — two partial buffers for DMA ping-pong */
#define LV_BUF_LINES   20
static lv_color_t s_buf1[TFT_WIDTH * LV_BUF_LINES];
static lv_color_t s_buf2[TFT_WIDTH * LV_BUF_LINES];
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static screen_id_t s_current_screen = SCREEN_MAIN_MENU;
static bool s_suspended = false;
static TaskHandle_t s_task_handle;

/* ── LVGL display flush callback ───────────────────────────────── */

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    tft_set_window(area->x1, area->y1, area->x2, area->y2);
    tft_push_pixels((const uint16_t *)color_p, (uint32_t)w * h);

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
    const EventBits_t watch_bits = PRINT_EVT_FINISHED | PRINT_EVT_CANCELLED | PRINT_EVT_ERROR;

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(eg, watch_bits, pdTRUE, pdFALSE, portMAX_DELAY);

        if (s_current_screen != SCREEN_PRINTING) continue;

        /* Print ended — turn off backlight and restore LVGL */
        backlight_set(0);
        tft_set_rotation(3);
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
        } else if (bits & PRINT_EVT_ERROR) {
            ui_navigate(SCREEN_MAIN_MENU);
            ESP_LOGW(TAG, "Print error — returning to menu");
        }
    }
}

/* ── UI task ───────────────────────────────────────────────────── */

static void ui_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        if (!s_suspended) {
            lv_timer_handler();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

/* ── Public API ────────────────────────────────────────────────── */

esp_err_t ui_init(QueueHandle_t input_queue)
{
    /* Initialize LVGL */
    lv_init();

    /* Setup draw buffers */
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, TFT_WIDTH * LV_BUF_LINES);

    /* Register display driver */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = TFT_WIDTH;
    s_disp_drv.ver_res = TFT_HEIGHT;
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

    /* Set TFT to menu rotation */
    tft_set_rotation(3);

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

    ESP_LOGI(TAG, "LVGL UI initialized (%dx%d, buf=%d lines)", TFT_WIDTH, TFT_HEIGHT, LV_BUF_LINES);
    return ESP_OK;
}

void ui_navigate(screen_id_t screen)
{
    lv_obj_t *scr = NULL;

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
    case SCREEN_PRINTING:
        /* Printing screen doesn't use LVGL — suspend and use TFT directly */
        ui_suspend();
        s_current_screen = SCREEN_PRINTING;
        return;
    default:
        ESP_LOGW(TAG, "Unknown screen: %d", screen);
        return;
    }

    if (scr) {
        /* Use simple screen load without animation to avoid null pointer
         * crashes in LVGL's transform_point during screen transitions */
        lv_scr_load(scr);
        s_current_screen = screen;
        ESP_LOGI(TAG, "Navigated to screen %d", screen);
    }
}

screen_id_t ui_get_current_screen(void)
{
    return s_current_screen;
}

void ui_suspend(void)
{
    s_suspended = true;
    ESP_LOGI(TAG, "LVGL suspended (TFT used for print mask)");
}

void ui_resume(void)
{
    s_suspended = false;
    /* Force full screen redraw */
    lv_obj_invalidate(lv_scr_act());
    ESP_LOGI(TAG, "LVGL resumed");
}
