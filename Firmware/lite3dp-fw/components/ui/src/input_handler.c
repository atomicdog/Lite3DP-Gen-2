#include "input_handler.h"
#include "hal_buttons.h"
#include "touch_input.h"
#include "esp_log.h"

static const char *TAG = "input";

static QueueHandle_t s_input_queue;
static lv_indev_drv_t s_indev_drv;

static void pointer_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool last_pressed = false;
    static int hold_count = 0;

    touch_point_t pt;
    touch_input_read(&pt);

    data->point.x = pt.x;
    data->point.y = pt.y;
    data->state = pt.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

    if (pt.pressed && !last_pressed) {
        ESP_LOGI(TAG, "TOUCH DN x=%d y=%d (raw %d,%d)", pt.x, pt.y, pt.raw_x, pt.raw_y);
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
