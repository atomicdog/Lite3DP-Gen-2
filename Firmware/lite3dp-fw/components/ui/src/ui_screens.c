#include "ui_screens.h"
#include "ui_manager.h"
#include "print_engine.h"
#include "print_params.h"
#include "profile_store.h"
#include "slicer_detect.h"
#include "sd_card.h"
#include "hal_motor.h"
#include "hal_uv_led.h"
#include "hal_gpio.h"
#include "tft_driver.h"
#include "touch_input.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *TAG = "ui_scr";

/* ── Color palette ─────────────────────────────────────────────── */

#define COL_BG          lv_color_hex(0x1A1A2E)
#define COL_CARD        lv_color_hex(0x16213E)
#define COL_ACCENT      lv_color_hex(0x0F3460)
#define COL_HIGHLIGHT   lv_color_hex(0xE94560)
#define COL_TEXT        lv_color_hex(0xEEEEEE)
#define COL_TEXT_DIM    lv_color_hex(0x888888)
#define COL_GREEN       lv_color_hex(0x2ECC71)

/* ── Layout metrics ────────────────────────────────────────────── */
/* Screens run landscape (480x320). Derive sizes from the live display
 * so a rotation change doesn't strand hard-coded coordinates. */

#define UI_MENU_ROTATION    3
#define UI_MASK_ROTATION    2

static inline lv_coord_t scr_w(void) { return lv_disp_get_hor_res(NULL); }
static inline lv_coord_t scr_h(void) { return lv_disp_get_ver_res(NULL); }

/* Body area below the title bar */
static inline lv_coord_t content_w(void) { return scr_w() - 40; }
static inline lv_coord_t content_h(void) { return scr_h() - 70; }

/* ── Shared state: selected print job ──────────────────────────── */

static print_job_t s_pending_job;
static print_profile_t s_edit_profile;  /* Profile being edited */

/* ── Cached screen objects ─────────────────────────────────────── */

static lv_obj_t *s_scr_main_menu = NULL;
static lv_obj_t *s_scr_file_browser = NULL;
static lv_obj_t *s_scr_print_preview = NULL;
static lv_obj_t *s_scr_settings = NULL;
static lv_obj_t *s_scr_profile_editor = NULL;
static lv_obj_t *s_scr_calibration = NULL;
static lv_obj_t *s_scr_utilities = NULL;
static lv_obj_t *s_scr_print_done = NULL;
static lv_obj_t *s_scr_wifi_status = NULL;

/* Dynamic labels */
static lv_obj_t *s_done_layers_label = NULL;
static lv_obj_t *s_done_time_label = NULL;
static lv_obj_t *s_preview_info_label = NULL;
static lv_obj_t *s_cal_offset_label = NULL;

/* ── Helper: create a styled button ────────────────────────────── */

static lv_obj_t *create_menu_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 300, 44);
    lv_obj_set_style_bg_color(btn, COL_ACCENT, 0);
    lv_obj_set_style_bg_color(btn, COL_HIGHLIGHT, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(btn, 8, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

/* Helper: create back button */
static lv_obj_t *create_back_btn(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 80, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn, COL_CARD, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl);
    return btn;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Main Menu
 * ══════════════════════════════════════════════════════════════════ */

static void on_print_clicked(lv_event_t *e)    { ui_navigate(SCREEN_FILE_BROWSER); }
static void on_settings_clicked(lv_event_t *e)  { ui_navigate(SCREEN_SETTINGS); }
static void on_utils_clicked(lv_event_t *e)     { ui_navigate(SCREEN_UTILITIES); }
static void on_wifi_clicked(lv_event_t *e)      { ui_navigate(SCREEN_WIFI_STATUS); }

lv_obj_t *ui_screen_main_menu(void)
{
    if (s_scr_main_menu) return s_scr_main_menu;

    s_scr_main_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_main_menu, COL_BG, 0);

    lv_obj_t *title = lv_label_create(s_scr_main_menu);
    lv_label_set_text(title, "Lite3DP Gen 2");
    lv_obj_set_style_text_color(title, COL_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *cont = lv_obj_create(s_scr_main_menu);
    lv_obj_set_size(cont, content_w(), content_h());
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    create_menu_btn(cont, LV_SYMBOL_PLAY " Print", on_print_clicked);
    create_menu_btn(cont, LV_SYMBOL_SETTINGS " Settings", on_settings_clicked);
    create_menu_btn(cont, LV_SYMBOL_LIST " Utilities", on_utils_clicked);
    create_menu_btn(cont, LV_SYMBOL_WIFI " WiFi", on_wifi_clicked);

    /* Group not needed for pointer/touch input — it causes mis-routed
     * events when a tap lands between buttons (LVGL sends click to the
     * focused group member instead of the touched object). */

    return s_scr_main_menu;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: File Browser
 * ══════════════════════════════════════════════════════════════════ */

static void on_folder_selected(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    /* Get the label text — in lv_list_add_btn, child 1 is the text label */
    lv_obj_t *label = lv_obj_get_child(btn, 1);
    if (!label) label = lv_obj_get_child(btn, 0);
    const char *name = lv_label_get_text(label);

    ESP_LOGI(TAG, "Selected folder: %s", name);

    /* Same assembly the web API uses (see print_job_build) */
    esp_err_t err = print_job_build(name, &s_pending_job);
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "Rejected folder name: %s", name);
        return;
    }
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "No layer PNGs in %s", name);
        /* Still show the preview — it reports 0 layers */
    }

    /* Force preview screen recreation with new data */
    if (s_scr_print_preview) {
        lv_obj_clean(s_scr_print_preview);
    }

    ui_navigate(SCREEN_PRINT_PREVIEW);
}

static void on_file_back(lv_event_t *e) { ui_navigate(SCREEN_MAIN_MENU); }

lv_obj_t *ui_screen_file_browser(void)
{
    if (s_scr_file_browser) {
        /* Clean children instead of deleting — can't delete active screen */
        lv_obj_clean(s_scr_file_browser);
    } else {
        s_scr_file_browser = lv_obj_create(NULL);
    }
    lv_obj_set_style_bg_color(s_scr_file_browser, COL_BG, 0);

    lv_obj_t *header = lv_label_create(s_scr_file_browser);
    lv_label_set_text(header, "Select Print File");
    lv_obj_set_style_text_color(header, COL_TEXT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);

    create_back_btn(s_scr_file_browser, on_file_back);

    lv_obj_t *list = lv_list_create(s_scr_file_browser);
    lv_obj_set_size(list, content_w(), content_h());
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(list, COL_CARD, 0);

    sd_entry_t entries[SD_MAX_ENTRIES];
    int count = 0;
    int dir_count = 0;
    if (sd_list_dir("", entries, SD_MAX_ENTRIES, &count) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            if (entries[i].is_dir) {
                lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_DIRECTORY, entries[i].name);
                lv_obj_add_event_cb(btn, on_folder_selected, LV_EVENT_CLICKED, NULL);
                dir_count++;
            }
        }
    }

    if (dir_count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "No print folders found.\nInsert SD card with\nsliced PNG files.");
        lv_obj_set_style_text_color(empty, COL_TEXT_DIM, 0);
    }

    return s_scr_file_browser;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Print Preview — shows job details before starting
 * ══════════════════════════════════════════════════════════════════ */

static void on_start_print(lv_event_t *e)
{
    ESP_LOGI(TAG, "Starting print: %s (%d layers, slicer=%s)",
             s_pending_job.folder_name, s_pending_job.total_layers,
             slicer_type_name(s_pending_job.slicer));

    /* The UI transition is driven by PRINT_EVT_STARTED in print_monitor_task,
     * so touch-started and web-started prints suspend LVGL the same way. */
    esp_err_t err = print_start(&s_pending_job);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "print_start rejected: %s", esp_err_to_name(err));
    }
}

static void on_preview_back(lv_event_t *e) { ui_navigate(SCREEN_FILE_BROWSER); }

lv_obj_t *ui_screen_print_preview(void)
{
    if (s_scr_print_preview) {
        lv_obj_clean(s_scr_print_preview);
    } else {
        s_scr_print_preview = lv_obj_create(NULL);
    }
    lv_obj_set_style_bg_color(s_scr_print_preview, COL_BG, 0);

    create_back_btn(s_scr_print_preview, on_preview_back);

    lv_obj_t *title = lv_label_create(s_scr_print_preview);
    lv_label_set_text(title, "Print Preview");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Folder name */
    lv_obj_t *folder_lbl = lv_label_create(s_scr_print_preview);
    lv_obj_set_style_text_color(folder_lbl, COL_HIGHLIGHT, 0);
    lv_label_set_text(folder_lbl, s_pending_job.folder_name);
    lv_obj_align(folder_lbl, LV_ALIGN_TOP_MID, 0, 30);

    /* Job info card — sits between the header and the Start button */
    lv_obj_t *card = lv_obj_create(s_scr_print_preview);
    lv_obj_set_size(card, content_w(), scr_h() - 120);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_radius(card, 12, 0);

    const print_profile_t *p = &s_pending_job.profile;
    char info_buf[320];

    /* Estimate print time */
    float avg_expo = (p->bottom_layers * (float)p->bottom_exposure +
                     (s_pending_job.total_layers - p->bottom_layers) * p->exposure_time)
                     / (float)(s_pending_job.total_layers > 0 ? s_pending_job.total_layers : 1);
    float lift_time = p->lift_height / p->lift_speed + p->lift_height / p->retract_speed;
    float layer_time = avg_expo + lift_time + p->rest_time_ms / 1000.0f;
    int est_seconds = (int)(layer_time * s_pending_job.total_layers);
    int est_h = est_seconds / 3600;
    int est_m = (est_seconds % 3600) / 60;

    snprintf(info_buf, sizeof(info_buf),
        "Slicer: %s\n"
        "Layers: %d\n"
        "Layer height: %.3f mm\n"
        "Exposure: %.1f s\n"
        "Bottom expo: %d s (%d layers)\n"
        "Lift: %.1f mm @ %.1f mm/s\n"
        "Retract: %.1f mm/s\n"
        "UV power: %d/255\n\n"
        "Est. time: %dh %dm",
        slicer_type_name(s_pending_job.slicer),
        s_pending_job.total_layers,
        (double)p->layer_height,
        (double)p->exposure_time,
        p->bottom_exposure, p->bottom_layers,
        (double)p->lift_height, (double)p->lift_speed,
        (double)p->retract_speed,
        p->uv_power,
        est_h, est_m
    );

    s_preview_info_label = lv_label_create(card);
    lv_label_set_text(s_preview_info_label, info_buf);
    lv_obj_set_style_text_color(s_preview_info_label, COL_TEXT, 0);
    lv_obj_align(s_preview_info_label, LV_ALIGN_TOP_LEFT, 10, 10);

    /* Start button */
    lv_obj_t *start_btn = lv_btn_create(s_scr_print_preview);
    lv_obj_set_size(start_btn, 200, 44);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(start_btn, COL_GREEN, 0);
    lv_obj_set_style_bg_color(start_btn, COL_HIGHLIGHT, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(start_btn, 12, 0);
    lv_obj_add_event_cb(start_btn, on_start_print, LV_EVENT_CLICKED, NULL);

    lv_obj_t *start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, LV_SYMBOL_PLAY " Start Print");
    lv_obj_center(start_lbl);

    return s_scr_print_preview;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Settings
 * ══════════════════════════════════════════════════════════════════ */

static void on_profile_clicked(lv_event_t *e) { ui_navigate(SCREEN_PROFILE_EDITOR); }
static void on_cal_clicked(lv_event_t *e)     { ui_navigate(SCREEN_CALIBRATION); }
static void on_settings_back(lv_event_t *e)   { ui_navigate(SCREEN_MAIN_MENU); }

static void on_save_profile(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    profile_save(slot, &s_edit_profile);
    ESP_LOGI(TAG, "Profile saved to slot %d", slot);
}

static void on_load_profile(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    profile_load(slot, &s_edit_profile);
    profile_save(0, &s_edit_profile);  /* Set as active */
    ESP_LOGI(TAG, "Profile loaded from slot %d", slot);
    /* Refresh editor */
    ui_navigate(SCREEN_PROFILE_EDITOR);
}

lv_obj_t *ui_screen_settings(void)
{
    if (s_scr_settings) return s_scr_settings;

    s_scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_settings, COL_BG, 0);

    lv_obj_t *title = lv_label_create(s_scr_settings);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    create_back_btn(s_scr_settings, on_settings_back);

    lv_obj_t *cont = lv_obj_create(s_scr_settings);
    lv_obj_set_size(cont, content_w(), content_h());
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);

    create_menu_btn(cont, "Edit Profile", on_profile_clicked);
    create_menu_btn(cont, "Calibration", on_cal_clicked);

    /* Profile save/load slots */
    lv_obj_t *slot_title = lv_label_create(cont);
    lv_label_set_text(slot_title, "Profile Slots:");
    lv_obj_set_style_text_color(slot_title, COL_TEXT_DIM, 0);

    for (int i = 1; i <= 6; i++) {
        lv_obj_t *row = lv_obj_create(cont);
        lv_obj_set_size(row, content_w() - 30, 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 2, 0);

        char lbl_buf[16];
        snprintf(lbl_buf, sizeof(lbl_buf), "Slot %c", 'A' + i - 1);
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, lbl_buf);
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *load_btn = lv_btn_create(row);
        lv_obj_set_size(load_btn, 60, 30);
        lv_obj_align(load_btn, LV_ALIGN_RIGHT_MID, -70, 0);
        lv_obj_set_style_bg_color(load_btn, COL_ACCENT, 0);
        lv_obj_add_event_cb(load_btn, on_load_profile, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *ld_lbl = lv_label_create(load_btn);
        lv_label_set_text(ld_lbl, "Load");
        lv_obj_center(ld_lbl);

        lv_obj_t *save_btn = lv_btn_create(row);
        lv_obj_set_size(save_btn, 60, 30);
        lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(save_btn, COL_HIGHLIGHT, 0);
        lv_obj_add_event_cb(save_btn, on_save_profile, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *sv_lbl = lv_label_create(save_btn);
        lv_label_set_text(sv_lbl, "Save");
        lv_obj_center(sv_lbl);
    }

    return s_scr_settings;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Profile Editor (interactive spinboxes)
 * ══════════════════════════════════════════════════════════════════ */

/* Callback data for linking a spinbox to a profile field */
typedef struct {
    float *float_ptr;
    int   *int_ptr;
    float  scale;       /* spinbox value = real value / scale */
} param_binding_t;

static param_binding_t s_bindings[16];
static int s_binding_count = 0;

static void on_spinbox_changed(lv_event_t *e)
{
    lv_obj_t *spinbox = lv_event_get_target(e);
    param_binding_t *bind = (param_binding_t *)lv_event_get_user_data(e);
    int32_t val = lv_spinbox_get_value(spinbox);

    if (bind->float_ptr) {
        *bind->float_ptr = (float)val * bind->scale;
    } else if (bind->int_ptr) {
        *bind->int_ptr = val;
    }
}

static void on_editor_back(lv_event_t *e)
{
    /* Auto-save to active slot */
    profile_save(0, &s_edit_profile);
    ui_navigate(SCREEN_SETTINGS);
}

static lv_obj_t *add_spinbox_row(lv_obj_t *parent, const char *name,
                                  int32_t value, int32_t min_val, int32_t max_val,
                                  int digits, int decimal_pos, param_binding_t *bind)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, content_w() - 30, 42);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, COL_TEXT_DIM, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *spinbox = lv_spinbox_create(row);
    lv_spinbox_set_range(spinbox, min_val, max_val);
    lv_spinbox_set_digit_format(spinbox, digits, decimal_pos);
    lv_spinbox_set_value(spinbox, value);
    lv_obj_set_size(spinbox, 100, 36);
    lv_obj_align(spinbox, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(spinbox, COL_ACCENT, 0);
    lv_obj_set_style_text_color(spinbox, COL_TEXT, 0);
    lv_obj_set_style_border_color(spinbox, COL_HIGHLIGHT, LV_STATE_FOCUSED);

    lv_obj_add_event_cb(spinbox, on_spinbox_changed, LV_EVENT_VALUE_CHANGED, bind);

    return spinbox;
}

lv_obj_t *ui_screen_profile_editor(void)
{
    s_binding_count = 0;
    if (s_scr_profile_editor) {
        lv_obj_clean(s_scr_profile_editor);
    } else {
        s_scr_profile_editor = lv_obj_create(NULL);
    }
    lv_obj_set_style_bg_color(s_scr_profile_editor, COL_BG, 0);

    lv_obj_t *title = lv_label_create(s_scr_profile_editor);
    lv_label_set_text(title, "Profile Editor");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    create_back_btn(s_scr_profile_editor, on_editor_back);

    /* Load current active profile for editing */
    profile_load(0, &s_edit_profile);

    lv_obj_t *cont = lv_obj_create(s_scr_profile_editor);
    lv_obj_set_size(cont, content_w(), content_h());
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(cont, COL_CARD, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    #define BIND_FLOAT(ptr, sc) do { \
        s_bindings[s_binding_count].float_ptr = (ptr); \
        s_bindings[s_binding_count].int_ptr = NULL; \
        s_bindings[s_binding_count].scale = (sc); \
    } while (0)
    #define BIND_INT(ptr) do { \
        s_bindings[s_binding_count].float_ptr = NULL; \
        s_bindings[s_binding_count].int_ptr = (ptr); \
        s_bindings[s_binding_count].scale = 1.0f; \
    } while (0)

    /* Layer Height: 0.025 - 0.100 mm, step 0.025 (stored as 25-100, scale 0.001) */
    BIND_FLOAT(&s_edit_profile.layer_height, 0.001f);
    add_spinbox_row(cont, "Layer (um)", (int32_t)(s_edit_profile.layer_height * 1000), 25, 100, 3, 0, &s_bindings[s_binding_count++]);

    /* Exposure time: 0.5 - 60.0s (stored as 5-600, scale 0.1) */
    BIND_FLOAT(&s_edit_profile.exposure_time, 0.1f);
    add_spinbox_row(cont, "Expo (s)", (int32_t)(s_edit_profile.exposure_time * 10), 5, 600, 4, 3, &s_bindings[s_binding_count++]);

    /* Bottom exposure: 1 - 120 s */
    BIND_INT(&s_edit_profile.bottom_exposure);
    add_spinbox_row(cont, "Bot Expo (s)", s_edit_profile.bottom_exposure, 1, 120, 3, 0, &s_bindings[s_binding_count++]);

    /* Bottom layers: 1 - 20 */
    BIND_INT(&s_edit_profile.bottom_layers);
    add_spinbox_row(cont, "Bot Layers", s_edit_profile.bottom_layers, 1, 20, 2, 0, &s_bindings[s_binding_count++]);

    /* Transition layers: 0 - 20 */
    BIND_INT(&s_edit_profile.transition_layers);
    add_spinbox_row(cont, "Trans Layers", s_edit_profile.transition_layers, 0, 20, 2, 0, &s_bindings[s_binding_count++]);

    /* Lift height: 1.0 - 15.0 mm (stored as 10-150, scale 0.1) */
    BIND_FLOAT(&s_edit_profile.lift_height, 0.1f);
    add_spinbox_row(cont, "Lift (mm)", (int32_t)(s_edit_profile.lift_height * 10), 10, 150, 3, 2, &s_bindings[s_binding_count++]);

    /* Initial lift height: 1.0 - 15.0 mm */
    BIND_FLOAT(&s_edit_profile.lift_height_initial, 0.1f);
    add_spinbox_row(cont, "Bot Lift (mm)", (int32_t)(s_edit_profile.lift_height_initial * 10), 10, 150, 3, 2, &s_bindings[s_binding_count++]);

    /* Lift speed: 0.3 - 5.0 mm/s (stored as 3-50, scale 0.1) */
    BIND_FLOAT(&s_edit_profile.lift_speed, 0.1f);
    add_spinbox_row(cont, "Lift Spd", (int32_t)(s_edit_profile.lift_speed * 10), 3, 50, 2, 1, &s_bindings[s_binding_count++]);

    /* Initial lift speed */
    BIND_FLOAT(&s_edit_profile.lift_speed_initial, 0.1f);
    add_spinbox_row(cont, "Bot Lift Spd", (int32_t)(s_edit_profile.lift_speed_initial * 10), 3, 50, 2, 1, &s_bindings[s_binding_count++]);

    /* Retract speed: 0.5 - 5.0 mm/s */
    BIND_FLOAT(&s_edit_profile.retract_speed, 0.1f);
    add_spinbox_row(cont, "Retract Spd", (int32_t)(s_edit_profile.retract_speed * 10), 5, 50, 2, 1, &s_bindings[s_binding_count++]);

    /* Rest time: 0 - 5000 ms */
    BIND_INT(&s_edit_profile.rest_time_ms);
    add_spinbox_row(cont, "Rest (ms)", s_edit_profile.rest_time_ms, 0, 5000, 4, 0, &s_bindings[s_binding_count++]);

    /* UV power: 0 - 255 */
    BIND_INT((int *)&s_edit_profile.uv_power);
    add_spinbox_row(cont, "UV Power", s_edit_profile.uv_power, 0, 255, 3, 0, &s_bindings[s_binding_count++]);

    #undef BIND_FLOAT
    #undef BIND_INT

    return s_scr_profile_editor;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Calibration (interactive Z-offset)
 * ══════════════════════════════════════════════════════════════════ */

static int32_t s_cal_offset = 0;

static void on_cal_up(lv_event_t *e)
{
    s_cal_offset += 100;
    motor_move_steps(MOTOR_DIR_UP, 100, 150);
    char buf[32];
    snprintf(buf, sizeof(buf), "Offset: %ld steps", (long)s_cal_offset);
    lv_label_set_text(s_cal_offset_label, buf);
}

static void on_cal_down(lv_event_t *e)
{
    s_cal_offset -= 100;
    motor_move_steps(MOTOR_DIR_DOWN, 100, 150);
    char buf[32];
    snprintf(buf, sizeof(buf), "Offset: %ld steps", (long)s_cal_offset);
    lv_label_set_text(s_cal_offset_label, buf);
}

static void on_cal_save(lv_event_t *e)
{
    print_profile_t prof;
    profile_load(0, &prof);
    prof.calibration_offset += s_cal_offset;
    profile_save(0, &prof);
    ESP_LOGI(TAG, "Calibration offset saved: %ld", (long)prof.calibration_offset);
    s_cal_offset = 0;
    ui_navigate(SCREEN_SETTINGS);
}

static void on_cal_home(lv_event_t *e)
{
    motor_enable();
    motor_home();
    s_cal_offset = 0;
    if (s_cal_offset_label) {
        lv_label_set_text(s_cal_offset_label, "Offset: 0 steps");
    }
}

static void on_cal_back(lv_event_t *e)
{
    motor_disable();
    ui_navigate(SCREEN_SETTINGS);
}

lv_obj_t *ui_screen_calibration(void)
{
    s_cal_offset = 0;
    if (s_scr_calibration) {
        lv_obj_clean(s_scr_calibration);
    } else {
        s_scr_calibration = lv_obj_create(NULL);
    }
    lv_obj_set_style_bg_color(s_scr_calibration, COL_BG, 0);

    create_back_btn(s_scr_calibration, on_cal_back);

    lv_obj_t *title = lv_label_create(s_scr_calibration);
    lv_label_set_text(title, "Z Calibration");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *info = lv_label_create(s_scr_calibration);
    lv_label_set_text(info, "Press Home, then adjust\nplatform height with UP/DOWN.\n100 steps per press.");
    lv_obj_set_style_text_color(info, COL_TEXT_DIM, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 30);

    /* Home button */
    lv_obj_t *home_btn = lv_btn_create(s_scr_calibration);
    lv_obj_set_size(home_btn, 200, 42);
    lv_obj_align(home_btn, LV_ALIGN_CENTER, 0, -46);
    lv_obj_set_style_bg_color(home_btn, COL_ACCENT, 0);
    lv_obj_add_event_cb(home_btn, on_cal_home, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_HOME " Home");
    lv_obj_center(home_lbl);

    /* Up/Down buttons */
    lv_obj_t *up_btn = lv_btn_create(s_scr_calibration);
    lv_obj_set_size(up_btn, 120, 42);
    lv_obj_align(up_btn, LV_ALIGN_CENTER, -65, 6);
    lv_obj_set_style_bg_color(up_btn, COL_ACCENT, 0);
    lv_obj_add_event_cb(up_btn, on_cal_up, LV_EVENT_CLICKED, NULL);
    lv_obj_t *up_lbl = lv_label_create(up_btn);
    lv_label_set_text(up_lbl, LV_SYMBOL_UP " Up");
    lv_obj_center(up_lbl);

    lv_obj_t *down_btn = lv_btn_create(s_scr_calibration);
    lv_obj_set_size(down_btn, 120, 42);
    lv_obj_align(down_btn, LV_ALIGN_CENTER, 65, 6);
    lv_obj_set_style_bg_color(down_btn, COL_ACCENT, 0);
    lv_obj_add_event_cb(down_btn, on_cal_down, LV_EVENT_CLICKED, NULL);
    lv_obj_t *down_lbl = lv_label_create(down_btn);
    lv_label_set_text(down_lbl, LV_SYMBOL_DOWN " Down");
    lv_obj_center(down_lbl);

    /* Offset display */
    s_cal_offset_label = lv_label_create(s_scr_calibration);
    lv_label_set_text(s_cal_offset_label, "Offset: 0 steps");
    lv_obj_set_style_text_color(s_cal_offset_label, COL_TEXT, 0);
    lv_obj_align(s_cal_offset_label, LV_ALIGN_CENTER, 0, 58);

    /* Save button */
    lv_obj_t *save_btn = lv_btn_create(s_scr_calibration);
    lv_obj_set_size(save_btn, 200, 42);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(save_btn, COL_GREEN, 0);
    lv_obj_add_event_cb(save_btn, on_cal_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE " Save Offset");
    lv_obj_center(save_lbl);

    return s_scr_calibration;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Utilities
 * ══════════════════════════════════════════════════════════════════ */

/* Timed utilities run off esp_timer rather than blocking inside the LVGL
 * event callback — a vTaskDelay here stalls the whole UI task (and the
 * touch indev) for the duration. */
static esp_timer_handle_t s_clean_vat_timer;
static esp_timer_handle_t s_test_uv_timer;

static esp_timer_handle_t get_oneshot(esp_timer_handle_t *slot,
                                      esp_timer_cb_t cb, const char *name)
{
    if (!*slot) {
        const esp_timer_create_args_t args = { .callback = cb, .name = name };
        esp_timer_create(&args, slot);
    }
    return *slot;
}

static void clean_vat_done(void *arg)
{
    uv_led_off();
    tft_fill_screen(0x0000);
    tft_set_rotation(UI_MENU_ROTATION);
    ui_resume();
    ESP_LOGI(TAG, "Clean vat cure finished");
}

static void on_clean_vat(lv_event_t *e)
{
    /* Expose full white screen for 10s to cure remaining resin.
     * LVGL is suspended so it can't repaint over the cure mask. */
    ui_suspend();
    tft_set_rotation(UI_MASK_ROTATION);
    tft_fill_screen(0xFFFF);
    uv_led_set_power(255);
    esp_timer_start_once(get_oneshot(&s_clean_vat_timer, clean_vat_done, "clean_vat"),
                         10ULL * 1000 * 1000);
}

static void test_uv_done(void *arg)
{
    uv_led_off();
}

static void on_test_uv(lv_event_t *e)
{
    uv_led_set_power(128);
    esp_timer_start_once(get_oneshot(&s_test_uv_timer, test_uv_done, "test_uv"),
                         3ULL * 1000 * 1000);
}

static void on_motor_test(lv_event_t *e)
{
    motor_enable();
    motor_move_mm(5.0f, 1.5f);
    /* Will lift 5mm, then the motor task handles it */
}

static void on_utils_back(lv_event_t *e) { ui_navigate(SCREEN_MAIN_MENU); }

lv_obj_t *ui_screen_utilities(void)
{
    if (s_scr_utilities) return s_scr_utilities;

    s_scr_utilities = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_utilities, COL_BG, 0);

    lv_obj_t *title = lv_label_create(s_scr_utilities);
    lv_label_set_text(title, LV_SYMBOL_LIST " Utilities");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    create_back_btn(s_scr_utilities, on_utils_back);

    lv_obj_t *cont = lv_obj_create(s_scr_utilities);
    lv_obj_set_size(cont, content_w(), content_h());
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_row(cont, 15, 0);

    create_menu_btn(cont, "Clean Vat (10s cure)", on_clean_vat);
    create_menu_btn(cont, "Test UV LED (3s)", on_test_uv);
    create_menu_btn(cont, "Motor Test (5mm lift)", on_motor_test);

    return s_scr_utilities;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Print Done
 * ══════════════════════════════════════════════════════════════════ */

static void on_done_ok(lv_event_t *e) { ui_navigate(SCREEN_MAIN_MENU); }

lv_obj_t *ui_screen_print_done(void)
{
    if (s_scr_print_done) {
        lv_obj_clean(s_scr_print_done);
    } else {
        s_scr_print_done = lv_obj_create(NULL);
    }
    lv_obj_set_style_bg_color(s_scr_print_done, COL_BG, 0);

    lv_obj_t *icon = lv_label_create(s_scr_print_done);
    lv_label_set_text(icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(icon, COL_GREEN, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *title = lv_label_create(s_scr_print_done);
    lv_label_set_text(title, "Print Complete!");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    s_done_layers_label = lv_label_create(s_scr_print_done);
    lv_label_set_text(s_done_layers_label, "Layers: ---");
    lv_obj_set_style_text_color(s_done_layers_label, COL_TEXT_DIM, 0);
    lv_obj_align(s_done_layers_label, LV_ALIGN_CENTER, 0, 10);

    s_done_time_label = lv_label_create(s_scr_print_done);
    lv_label_set_text(s_done_time_label, "Time: ---");
    lv_obj_set_style_text_color(s_done_time_label, COL_TEXT_DIM, 0);
    lv_obj_align(s_done_time_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *ok_btn = lv_btn_create(s_scr_print_done);
    lv_obj_set_size(ok_btn, 200, 50);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(ok_btn, COL_ACCENT, 0);
    lv_obj_add_event_cb(ok_btn, on_done_ok, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "OK");
    lv_obj_center(ok_lbl);

    return s_scr_print_done;
}

void ui_screen_print_done_set_results(int layers, int elapsed_s)
{
    if (s_done_layers_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Layers: %d", layers);
        lv_label_set_text(s_done_layers_label, buf);
    }
    if (s_done_time_label) {
        char buf[32];
        int h = elapsed_s / 3600;
        int m = (elapsed_s % 3600) / 60;
        int s = elapsed_s % 60;
        if (h > 0) snprintf(buf, sizeof(buf), "Time: %dh %dm %ds", h, m, s);
        else snprintf(buf, sizeof(buf), "Time: %dm %ds", m, s);
        lv_label_set_text(s_done_time_label, buf);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: WiFi Status
 * ══════════════════════════════════════════════════════════════════ */

static void on_wifi_back(lv_event_t *e) { ui_navigate(SCREEN_MAIN_MENU); }

lv_obj_t *ui_screen_wifi_status(void)
{
    if (s_scr_wifi_status) {
        lv_obj_clean(s_scr_wifi_status);
    } else {
        s_scr_wifi_status = lv_obj_create(NULL);
    }
    lv_obj_set_style_bg_color(s_scr_wifi_status, COL_BG, 0);

    create_back_btn(s_scr_wifi_status, on_wifi_back);

    lv_obj_t *title = lv_label_create(s_scr_wifi_status);
    lv_label_set_text(title, LV_SYMBOL_WIFI " WiFi Status");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* Dynamic WiFi info — ideally updated from wifi_manager state */
    lv_obj_t *card = lv_obj_create(s_scr_wifi_status);
    lv_obj_set_size(card, content_w(), scr_h() - 90);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_radius(card, 12, 0);

#ifdef CONFIG_LITE3DP_WIFI_ENABLED
    /* Try to get real WiFi info */
    typedef enum {
        UI_WIFI_IDLE,
        UI_WIFI_AP_ACTIVE,
        UI_WIFI_STA_CONNECTING,
        UI_WIFI_STA_CONNECTED,
        UI_WIFI_STA_DISCONNECTED,
    } ui_wifi_state_t;
    extern const char *wifi_get_ip_str(void);
    extern int wifi_get_state(void);

    ui_wifi_state_t state = (ui_wifi_state_t)wifi_get_state();
    const char *ip = wifi_get_ip_str();
    const char *mode_str;

    switch (state) {
    case UI_WIFI_AP_ACTIVE:      mode_str = "Access Point"; break;
    case UI_WIFI_STA_CONNECTED:  mode_str = "Connected (STA)"; break;
    case UI_WIFI_STA_CONNECTING: mode_str = "Connecting..."; break;
    default:                        mode_str = "Idle"; break;
    }

    char info_buf[200];
    snprintf(info_buf, sizeof(info_buf),
        "Mode: %s\n"
        "IP: %s\n\n"
        "Web UI:\n"
        "http://%s\n"
        "http://lite3dp.local",
        mode_str, ip, ip);

    lv_obj_t *info = lv_label_create(card);
    lv_label_set_text(info, info_buf);
#else
    lv_obj_t *info = lv_label_create(card);
    lv_label_set_text(info, "WiFi is disabled.\nEnable in Kconfig.");
#endif

    lv_obj_set_style_text_color(info, COL_TEXT, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 10, 10);

    return s_scr_wifi_status;
}

/* ══════════════════════════════════════════════════════════════════
 *  Screen: Touch Test / Calibration Debug
 * ══════════════════════════════════════════════════════════════════ */

static lv_obj_t *s_scr_touch_test = NULL;
static lv_obj_t *s_touch_cursor = NULL;
static lv_obj_t *s_touch_label = NULL;

static void touch_test_timer_cb(lv_timer_t *timer)
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    /* Check if touched by looking at IRQ pin (no SPI re-read) */
    if (touch_input_pressed()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "x=%d y=%d", p.x, p.y);
        lv_label_set_text(s_touch_label, buf);

        lv_obj_set_pos(s_touch_cursor, p.x - 8, p.y - 8);
        lv_obj_clear_flag(s_touch_cursor, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_touch_cursor, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_screen_touch_test(void)
{
    if (s_scr_touch_test) return s_scr_touch_test;

    s_scr_touch_test = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_touch_test, lv_color_black(), 0);
    lv_obj_clear_flag(s_scr_touch_test, LV_OBJ_FLAG_SCROLLABLE);

    /* Four colored quadrants to reveal display orientation.
     * LVGL coords: A=top-left, B=top-right, C=bottom-left, D=bottom-right.
     * Tell me which letter is at each PHYSICAL corner. */
    const lv_coord_t qw = scr_w() / 2, qh = scr_h() / 2;
    const struct { int x; int y; lv_color_t col; const char *lbl; } quads[] = {
        {  0,  0, {.full = 0xF800}, "A"},  /* Red    - LVGL top-left     */
        { qw,  0, {.full = 0x07E0}, "B"},  /* Green  - LVGL top-right    */
        {  0, qh, {.full = 0x001F}, "C"},  /* Blue   - LVGL bottom-left  */
        { qw, qh, {.full = 0xFFE0}, "D"},  /* Yellow - LVGL bottom-right */
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *r = lv_obj_create(s_scr_touch_test);
        lv_obj_set_pos(r, quads[i].x, quads[i].y);
        lv_obj_set_size(r, qw, qh);
        lv_obj_set_style_bg_color(r, quads[i].col, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_50, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_radius(r, 0, 0);
        lv_obj_set_style_pad_all(r, 0, 0);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lbl = lv_label_create(r);
        lv_label_set_text(lbl, quads[i].lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_40, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
    }

    /* Coordinate readout — large font, centered */
    s_touch_label = lv_label_create(s_scr_touch_test);
    lv_label_set_text(s_touch_label, "Tap screen");
    lv_obj_set_style_text_color(s_touch_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_touch_label, &lv_font_montserrat_20, 0);
    lv_obj_center(s_touch_label);

    /* Cursor dot — 16px white circle */
    s_touch_cursor = lv_obj_create(s_scr_touch_test);
    lv_obj_set_size(s_touch_cursor, 16, 16);
    lv_obj_set_style_bg_color(s_touch_cursor, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_touch_cursor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_touch_cursor, 0, 0);
    lv_obj_set_style_radius(s_touch_cursor, 8, 0);
    lv_obj_set_style_pad_all(s_touch_cursor, 0, 0);
    lv_obj_clear_flag(s_touch_cursor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_touch_cursor, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(touch_test_timer_cb, 33, NULL);

    return s_scr_touch_test;
}
