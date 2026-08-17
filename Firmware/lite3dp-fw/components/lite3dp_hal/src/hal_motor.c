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

/* Stepping is a busy-wait (esp_rom_delay_us), and the motor task is pinned to
 * core 1 at priority 7 — above the UI task on the same core. A 6 mm bottom
 * lift at 0.7 mm/s is 31476 steps of ~272 us, i.e. 8.6 s of solid spinning,
 * which starves IDLE1 and trips the task watchdog (observed twice per bottom
 * layer). Yield for a tick roughly this often so the scheduler gets a look in.
 * Derived from the step rate rather than a fixed step count so the cost stays
 * ~1 tick per 100 ms of stepping (~1%) at any speed. */
#define STEP_YIELD_MS       100

/* Homing speed: slower for safety */
#define HOME_SPEED_MM_S     1.0f
/* Generous upper bound on Z travel — this only has to catch a broken
 * endstop, not measure the machine. */
#define HOME_MAX_TRAVEL_MM  200.0f
#define HOME_DELAY_US       ((uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * HOME_SPEED_MM_S)))

static QueueHandle_t       s_cmd_queue;
static EventGroupHandle_t  s_notify_eg;
static EventBits_t         s_idle_bit;
static TaskHandle_t        s_task_handle;

/* One step pulse at the given rate. Busy-waits: the delays are far too short
 * (tens to hundreds of microseconds) for vTaskDelay's 1 ms tick. */
static inline void step_pulse(uint32_t delay_us)
{
    gpio_set_level(PIN_MOTOR_STEP, 1);
    esp_rom_delay_us(delay_us / 2);
    gpio_set_level(PIN_MOTOR_STEP, 0);
    esp_rom_delay_us(delay_us / 2);
}

/* How many steps may run back-to-back before the task must yield. */
static uint32_t steps_per_yield(uint32_t delay_us)
{
    if (delay_us == 0) {
        return 1024;
    }
    uint32_t n = (STEP_YIELD_MS * 1000u) / delay_us;
    /* A step slower than the yield interval already yields every step */
    return n ? n : 1;
}

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

        /* Generate step pulses, yielding periodically so this task does not
         * hog its core for the whole move. A stepper holds position across the
         * pause, and there is no acceleration ramp here anyway, so the brief
         * gap is mechanically no different from the start/stop at either end. */
        const uint32_t yield_every = steps_per_yield(cmd.delay_us);
        uint32_t since_yield = 0;

        for (uint32_t i = 0; i < cmd.steps; i++) {
            step_pulse(cmd.delay_us);

            if (++since_yield >= yield_every) {
                since_yield = 0;
                vTaskDelay(1);
            }
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
    if (mm == 0.0f || speed_mm_s <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Sign selects direction: positive lifts away from the vat, negative
     * descends toward it. */
    motor_dir_t dir = (mm < 0.0f) ? MOTOR_DIR_DOWN : MOTOR_DIR_UP;

    uint32_t steps    = (uint32_t)roundf(fabsf(mm) * MOTOR_STEPS_PER_MM);
    uint32_t delay_us = (uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * speed_mm_s));

    if (steps == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return motor_move_steps(dir, steps, delay_us);
}

esp_err_t motor_home(void)
{
    ESP_LOGI(TAG, "Homing: descending to endstop...");

    if (endstop_triggered()) {
        ESP_LOGI(TAG, "Already at endstop");
        return ESP_OK;
    }

    motor_enable();
    gpio_set_level(PIN_MOTOR_DIR, 0);  /* Down */

    const uint32_t max_steps = (uint32_t)(HOME_MAX_TRAVEL_MM * MOTOR_STEPS_PER_MM);
    const uint32_t yield_every = steps_per_yield(HOME_DELAY_US);
    uint32_t steps = 0;
    uint32_t since_yield = 0;

    while (!endstop_triggered()) {
        /* Bounded: a disconnected or failed switch would otherwise drive
         * the platform down through the FEP and the masking LCD. */
        if (++steps > max_steps) {
            ESP_LOGE(TAG, "Homing aborted: no endstop within %.0f mm",
                     (double)HOME_MAX_TRAVEL_MM);
            return ESP_ERR_TIMEOUT;
        }

        step_pulse(HOME_DELAY_US);

        /* A full descent takes minutes of busy-looping; yield so the calling
         * task (HTTP or UI) doesn't starve its core or trip the watchdog.
         * This one runs in the caller's context, not the motor task. */
        if (++since_yield >= yield_every) {
            since_yield = 0;
            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "Endstop reached after %.2f mm",
             (double)((float)steps / MOTOR_STEPS_PER_MM));
    return ESP_OK;
}

QueueHandle_t motor_get_queue(void)
{
    return s_cmd_queue;
}
