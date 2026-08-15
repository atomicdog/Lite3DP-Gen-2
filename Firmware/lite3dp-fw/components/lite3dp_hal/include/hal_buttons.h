#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    BTN_UP,
    BTN_DOWN,
    BTN_NEXT,
    BTN_BACK,
    BTN_PLAY,
    BTN_COUNT,
} button_id_t;

typedef enum {
    BTN_EVT_PRESSED,
    BTN_EVT_RELEASED,
    BTN_EVT_HELD,
} button_event_t;

typedef struct {
    button_id_t   id;
    button_event_t event;
} button_msg_t;

/**
 * Initialize button inputs and start the polling task.
 * Button events are posted to the provided queue.
 * @param event_queue  Queue of button_msg_t, created by caller
 */
esp_err_t buttons_init(QueueHandle_t event_queue);
