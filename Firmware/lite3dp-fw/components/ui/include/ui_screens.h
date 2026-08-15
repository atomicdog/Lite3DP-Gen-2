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
