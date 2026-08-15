#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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
