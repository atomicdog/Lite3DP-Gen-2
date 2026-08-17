#pragma once

#include "lvgl.h"

/* Each screen creation function returns the lv_obj_t screen object.
 * Screens are created lazily and cached. */

lv_obj_t *ui_screen_main_menu(void);
lv_obj_t *ui_screen_file_browser(void);
lv_obj_t *ui_screen_print_preview(void);
lv_obj_t *ui_screen_settings(void);
lv_obj_t *ui_screen_profile_editor(void);
lv_obj_t *ui_screen_calibration(void);
lv_obj_t *ui_screen_utilities(void);
lv_obj_t *ui_screen_print_done(void);
lv_obj_t *ui_screen_wifi_status(void);

/** Update the print-done screen with results. */
void ui_screen_print_done_set_results(int layers, int elapsed_s);

/** Touch test/calibration screen — boots first for diagnostics. */
lv_obj_t *ui_screen_touch_test(void);

/* ── Touch calibration capture ─────────────────────────────────── */

/** One crosshair target and the raw ADC pair measured when it was tapped. */
typedef struct {
    uint16_t target_x, target_y;   /* where the crosshair was drawn */
    uint16_t raw_x, raw_y;         /* raw ADC at the tap            */
    uint16_t pressure;
} touch_cal_point_t;

#define TOUCH_CAL_POINTS  5

/** Put the test screen into crosshair mode and clear captured points. */
void ui_touch_cal_start(void);

/**
 * Points captured so far, oldest first. Returns the count and, via `out`, a
 * pointer to the internal array — valid until the next capture.
 */
int ui_touch_cal_points(const touch_cal_point_t **out);
