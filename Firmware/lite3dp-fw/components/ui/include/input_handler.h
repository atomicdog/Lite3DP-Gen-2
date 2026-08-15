#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

/**
 * Register the XPT2046 touchscreen as an LVGL pointer input device.
 *
 * @param input_queue  Queue of button_msg_t (for physical buttons)
 * @return LVGL input device pointer
 */
lv_indev_t *input_handler_register(QueueHandle_t input_queue);
