#include "hal_buttons.h"
#include "hal_gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/touch_pad.h"
#include "driver/gpio.h"

static const char *TAG = "hal_buttons";

#define BUTTONS_TASK_STACK  2048
#define BUTTONS_TASK_PRIO   4
#define BUTTONS_TASK_CORE   1
#define POLL_INTERVAL_MS    20      /* 50 Hz */
#define HOLD_THRESHOLD_MS   500

/* Touch pad mapping (ESP32 touch pad numbers for the GPIOs) */
#define TOUCH_PAD_UP    TOUCH_PAD_NUM5   /* GPIO12 */
#define TOUCH_PAD_DOWN  TOUCH_PAD_NUM6   /* GPIO14 */
#define TOUCH_PAD_NEXT  TOUCH_PAD_NUM4   /* GPIO13 */
#define TOUCH_PAD_BACK  TOUCH_PAD_NUM9   /* GPIO32 */

static QueueHandle_t s_event_queue;

typedef struct {
    bool     pressed;
    uint32_t press_start_ms;
    bool     hold_sent;
} btn_state_t;

static btn_state_t s_state[BTN_COUNT];

/* ESP-IDF touch_pad_read() returns raw capacitance values.
 * Untouched: ~500-1000+, Touched: drops significantly.
 * We use touch_pad_read() for stability after calibration.
 * Threshold is set per-pad based on calibration baseline. */

static uint16_t s_touch_baseline[4];  /* Baseline (untouched) values */
static const touch_pad_t s_touch_pads[4] = {
    TOUCH_PAD_UP, TOUCH_PAD_DOWN, TOUCH_PAD_NEXT, TOUCH_PAD_BACK
};

static bool read_touch_button(int idx)
{
    uint16_t val = 0;
    touch_pad_read(s_touch_pads[idx], &val);
    /* Button is pressed when value drops below 70% of baseline */
    return val < (s_touch_baseline[idx] * 7 / 10);
}

static bool read_button_raw(button_id_t id)
{
    switch (id) {
    case BTN_UP:    return read_touch_button(0);
    case BTN_DOWN:  return read_touch_button(1);
    case BTN_NEXT:  return read_touch_button(2);
    case BTN_BACK:  return read_touch_button(3);
    case BTN_PLAY:  return gpio_get_level(PIN_BTN_PLAY) == 0;
    default:        return false;
    }
}

static void send_event(button_id_t id, button_event_t event)
{
    button_msg_t msg = { .id = id, .event = event };
    xQueueSend(s_event_queue, &msg, 0);
}

static uint32_t s_debug_counter = 0;

static void buttons_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Debug: print raw touch values every 2 seconds */
        if (++s_debug_counter % 100 == 0) {
            uint16_t vals[4];
            for (int i = 0; i < 4; i++) {
                touch_pad_read(s_touch_pads[i], &vals[i]);
            }
            ESP_LOGI(TAG, "Touch raw: UP=%u DOWN=%u NEXT=%u BACK=%u (base: %u %u %u %u)",
                     vals[0], vals[1], vals[2], vals[3],
                     s_touch_baseline[0], s_touch_baseline[1],
                     s_touch_baseline[2], s_touch_baseline[3]);
        }

        for (int i = 0; i < BTN_COUNT; i++) {
            bool pressed = read_button_raw((button_id_t)i);
            btn_state_t *st = &s_state[i];

            if (pressed && !st->pressed) {
                /* Just pressed */
                st->pressed = true;
                st->press_start_ms = now_ms;
                st->hold_sent = false;
                ESP_LOGI(TAG, "Button %d PRESSED", i);
                send_event((button_id_t)i, BTN_EVT_PRESSED);
            } else if (!pressed && st->pressed) {
                /* Just released */
                st->pressed = false;
                send_event((button_id_t)i, BTN_EVT_RELEASED);
            } else if (pressed && st->pressed && !st->hold_sent) {
                /* Check for hold */
                if ((now_ms - st->press_start_ms) >= HOLD_THRESHOLD_MS) {
                    st->hold_sent = true;
                    send_event((button_id_t)i, BTN_EVT_HELD);
                }
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t buttons_init(QueueHandle_t event_queue)
{
    ESP_LOGI(TAG, "buttons_init: start (heap=%lu)", (unsigned long)esp_get_free_heap_size());
    s_event_queue = event_queue;
    memset(s_state, 0, sizeof(s_state));

    ESP_LOGI(TAG, "buttons_init: calling touch_pad_init");
    esp_err_t ret = touch_pad_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch pad init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "buttons_init: touch_pad_init OK");

    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    ESP_LOGI(TAG, "buttons_init: configuring pads");
    touch_pad_config(TOUCH_PAD_UP, 0);
    touch_pad_config(TOUCH_PAD_DOWN, 0);
    touch_pad_config(TOUCH_PAD_NEXT, 0);
    touch_pad_config(TOUCH_PAD_BACK, 0);

    ESP_LOGI(TAG, "buttons_init: reading baselines");
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int i = 0; i < 4; i++) {
        touch_pad_read(s_touch_pads[i], &s_touch_baseline[i]);
        ESP_LOGI(TAG, "Touch pad %d baseline: %u", i, s_touch_baseline[i]);
    }
    ESP_LOGI(TAG, "buttons_init: heap=%lu", (unsigned long)esp_get_free_heap_size());

    BaseType_t xret = xTaskCreatePinnedToCore(
        buttons_task, "buttons", BUTTONS_TASK_STACK, NULL,
        BUTTONS_TASK_PRIO, NULL, BUTTONS_TASK_CORE
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create buttons task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buttons initialized (capacitive touch + GPIO)");
    return ESP_OK;
}
