#include "hal_motor.h"
#include "hal_gpio.h"
#include "hal_endstop.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include <math.h>

static const char *TAG = "hal_motor";

#define MOTOR_QUEUE_DEPTH   8
#define MOTOR_TASK_STACK    4096
#define MOTOR_TASK_PRIO     7
#define MOTOR_TASK_CORE     1

/* Homing speed: slower for safety */
#define HOME_SPEED_MM_S     1.0f
#define HOME_DELAY_US       ((uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * HOME_SPEED_MM_S)))

static QueueHandle_t       s_cmd_queue;
static EventGroupHandle_t  s_notify_eg;
static EventBits_t         s_idle_bit;
static TaskHandle_t        s_task_handle;

static void motor_task(void *arg)
{
    motor_cmd_t cmd;

    for (;;) {
        /* Signal idle while waiting */
        xEventGroupSetBits(s_notify_eg, s_idle_bit);

        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Clear idle bit — we're moving */
        xEventGroupClearBits(s_notify_eg, s_idle_bit);

        /* Set direction */
        gpio_set_level(PIN_MOTOR_DIR, cmd.direction == MOTOR_DIR_UP ? 1 : 0);

        /* Generate step pulses */
        for (uint32_t i = 0; i < cmd.steps; i++) {
            gpio_set_level(PIN_MOTOR_STEP, 1);
            esp_rom_delay_us(cmd.delay_us / 2);
            gpio_set_level(PIN_MOTOR_STEP, 0);
            esp_rom_delay_us(cmd.delay_us / 2);
        }
    }
}

esp_err_t motor_init(EventGroupHandle_t notify_event_group, EventBits_t motor_idle_bit)
{
    s_notify_eg = notify_event_group;
    s_idle_bit  = motor_idle_bit;

    s_cmd_queue = xQueueCreate(MOTOR_QUEUE_DEPTH, sizeof(motor_cmd_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create motor command queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        motor_task, "motor", MOTOR_TASK_STACK, NULL,
        MOTOR_TASK_PRIO, &s_task_handle, MOTOR_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Motor initialized (%.0f steps/mm)", (double)MOTOR_STEPS_PER_MM);
    return ESP_OK;
}

esp_err_t motor_enable(void)
{
    gpio_set_level(PIN_MOTOR_EN, 0);  /* Active low */
    return ESP_OK;
}

esp_err_t motor_disable(void)
{
    gpio_set_level(PIN_MOTOR_EN, 1);
    return ESP_OK;
}

esp_err_t motor_move_steps(motor_dir_t direction, uint32_t steps, uint32_t delay_us)
{
    motor_cmd_t cmd = {
        .direction = direction,
        .steps     = steps,
        .delay_us  = delay_us,
    };

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Motor command queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t motor_move_mm(float mm, float speed_mm_s)
{
    if (mm <= 0.0f || speed_mm_s <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t steps    = (uint32_t)roundf(mm * MOTOR_STEPS_PER_MM);
    uint32_t delay_us = (uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * speed_mm_s));

    /* Direction is determined by caller via motor_move_steps;
       this convenience function always moves "up" (positive).
       For descend, caller should use motor_move_steps directly. */
    return motor_move_steps(MOTOR_DIR_UP, steps, delay_us);
}

esp_err_t motor_home(void)
{
    ESP_LOGI(TAG, "Homing: descending to endstop...");

    motor_enable();
    gpio_set_level(PIN_MOTOR_DIR, 0);  /* Down */

    while (!endstop_triggered()) {
        gpio_set_level(PIN_MOTOR_STEP, 1);
        esp_rom_delay_us(HOME_DELAY_US / 2);
        gpio_set_level(PIN_MOTOR_STEP, 0);
        esp_rom_delay_us(HOME_DELAY_US / 2);
    }

    ESP_LOGI(TAG, "Endstop reached");
    return ESP_OK;
}

QueueHandle_t motor_get_queue(void)
{
    return s_cmd_queue;
}
