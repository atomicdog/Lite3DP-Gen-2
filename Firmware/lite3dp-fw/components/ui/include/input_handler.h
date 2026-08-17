#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "touch_input.h"

/**
 * Register the XPT2046 touchscreen as an LVGL pointer input device.
 *
 * @param input_queue  Queue of button_msg_t (for physical buttons)
 * @return LVGL input device pointer
 */
lv_indev_t *input_handler_register(QueueHandle_t input_queue);

/**
 * The most recent sample taken by the LVGL read callback, including raw ADC
 * values and pressure. Lets diagnostic screens see what the driver saw without
 * issuing a second SPI burst of their own.
 */
void input_handler_last_sample(touch_point_t *out);
