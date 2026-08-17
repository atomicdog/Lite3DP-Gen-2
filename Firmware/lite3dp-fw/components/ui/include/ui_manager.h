#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Panel orientation: the UI runs landscape, the exposure mask portrait
 * (mirrored, since it is viewed through the panel from the vat side). */
#define UI_MENU_ROTATION    3
#define UI_MASK_ROTATION    2

typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_FILE_BROWSER,
    SCREEN_PRINT_PREVIEW,
    SCREEN_SETTINGS,
    SCREEN_PROFILE_EDITOR,
    SCREEN_CALIBRATION,
    SCREEN_UTILITIES,
    SCREEN_PRINTING,        /* Active print — TFT used for mask, LVGL suspended */
    SCREEN_PRINT_DONE,
    SCREEN_WIFI_STATUS,
    SCREEN_TOUCH_TEST,      /* Diagnostics: quadrants, cursor, cal crosshairs */
    SCREEN_COUNT,
} screen_id_t;

/**
 * Initialize the LVGL-based UI system.
 * Creates the UI task, registers display/input drivers with LVGL.
 * @param input_queue  Queue of button_msg_t from button polling task
 */
esp_err_t ui_init(QueueHandle_t input_queue);

/** Navigate to a specific screen. */
void ui_navigate(screen_id_t screen);

/** Get the current active screen. */
screen_id_t ui_get_current_screen(void);

/**
 * Suspend LVGL rendering (called when TFT is needed for mask projection).
 * After calling this, the TFT can be used directly until ui_resume().
 */
void ui_suspend(void);

/** Resume LVGL rendering after mask projection is done. */
void ui_resume(void);

/**
 * LVGL is not thread-safe. Any LVGL call made outside the UI task
 * (print monitor, web handlers) must be wrapped in ui_lock()/ui_unlock().
 * The mutex is recursive, so callbacks running inside lv_timer_handler()
 * may take it again.
 */
void ui_lock(void);
void ui_unlock(void);

/**
 * LVGL pool statistics. LVGL allocates from its own static pool
 * (LV_MEM_SIZE_KILOBYTES), not the ESP heap, so free-heap numbers say nothing
 * about whether the UI is about to run out. Any argument may be NULL.
 */
void ui_mem_stats(uint32_t *free_bytes, uint32_t *free_biggest,
                  uint8_t *used_pct, uint8_t *frag_pct);

/**
 * Sink for screen capture. Called once per flushed area, in the caller's
 * task context, with big-endian RGB565 pixels for that rectangle.
 * Return ESP_OK to continue; anything else aborts the capture.
 */
typedef esp_err_t (*ui_capture_cb_t)(void *ctx, uint16_t x1, uint16_t y1,
                                     uint16_t x2, uint16_t y2,
                                     const void *pixels, size_t len);

/**
 * Force a full redraw of the active screen, streaming every flushed area
 * to `cb` (and to the panel as usual). Runs synchronously in the calling
 * task, so an HTTP handler may write the pixels straight to its response.
 * Pass cb = NULL to read back the screen geometry without redrawing.
 * Returns ESP_ERR_INVALID_STATE while LVGL is suspended for a print.
 */
esp_err_t ui_capture_screen(ui_capture_cb_t cb, void *ctx,
                            uint16_t *out_w, uint16_t *out_h);
