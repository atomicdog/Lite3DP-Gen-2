#include "input_handler.h"
#include "hal_buttons.h"
#include "touch_input.h"
#include "esp_log.h"

static const char *TAG = "input";

static QueueHandle_t s_input_queue;
static lv_indev_drv_t s_indev_drv;
static touch_point_t s_last_sample;

/* Last coordinates from an actual press. A release reports x/y of 0, so
 * reporting those to LVGL would land every click at the top-left corner. */
static lv_coord_t s_held_x = 0, s_held_y = 0;

static void pointer_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool last_pressed = false;
    static int hold_count = 0;

    touch_point_t pt;
    if (touch_input_read(&pt) != ESP_OK) {
        /* Lost the race for the SPI device this cycle. Repeat the last state —
         * reporting a release we did not measure would fire a phantom click. */
        data->point.x = s_held_x;
        data->point.y = s_held_y;
        data->state = s_last_sample.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        return;
    }
    s_last_sample = pt;

    if (pt.pressed) {
        s_held_x = pt.x;
        s_held_y = pt.y;
    }
    data->point.x = s_held_x;
    data->point.y = s_held_y;
    data->state = pt.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

    if (pt.pressed && !last_pressed) {
        ESP_LOGI(TAG, "TOUCH DN x=%d y=%d (raw %d,%d z=%d)",
                 pt.x, pt.y, pt.raw_x, pt.raw_y, pt.pressure);
        hold_count = 0;
    } else if (!pt.pressed && last_pressed) {
        ESP_LOGI(TAG, "TOUCH UP");
        hold_count = 0;
    } else if (pt.pressed && ++hold_count == 30) {
        ESP_LOGI(TAG, "TOUCH .. x=%d y=%d", pt.x, pt.y);
        hold_count = 0;
    }
    last_pressed = pt.pressed;
}

lv_indev_t *input_handler_register(QueueHandle_t input_queue)
{
    s_input_queue = input_queue;

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = pointer_read_cb;

    lv_indev_t *indev = lv_indev_drv_register(&s_indev_drv);
    ESP_LOGI(TAG, "Registered LVGL pointer input (XPT2046 touch)");
    return indev;
}

void input_handler_last_sample(touch_point_t *out)
{
    if (out) *out = s_last_sample;
}
