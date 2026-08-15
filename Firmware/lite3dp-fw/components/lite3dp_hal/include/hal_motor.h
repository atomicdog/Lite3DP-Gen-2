#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    MOTOR_DIR_UP   = 1,   /* Lift platform (away from vat) */
    MOTOR_DIR_DOWN = -1,  /* Lower platform (toward vat) */
} motor_dir_t;

typedef struct {
    motor_dir_t direction;
    uint32_t    steps;
    uint32_t    delay_us;   /* Microseconds between step pulses */
} motor_cmd_t;

/**
 * Initialize motor GPIO and create the motor task.
 * The motor task consumes commands from its internal queue.
 * @param notify_event_group  Event group to set MOTOR_IDLE bit on completion
 * @param motor_idle_bit      Bit index in the event group
 */
esp_err_t motor_init(EventGroupHandle_t notify_event_group, EventBits_t motor_idle_bit);

/** Enable the stepper driver (active low EN pin). */
esp_err_t motor_enable(void);

/** Disable the stepper driver (saves power, releases holding torque). */
esp_err_t motor_disable(void);

/**
 * Queue a movement command (non-blocking).
 * The motor task will execute it and set the MOTOR_IDLE event bit on completion.
 */
esp_err_t motor_move_steps(motor_dir_t direction, uint32_t steps, uint32_t delay_us);

/**
 * Convenience: move a distance in mm at a given speed.
 * Internally converts to steps and delay, then queues the command.
 */
esp_err_t motor_move_mm(float mm, float speed_mm_s);

/**
 * Home the platform: descend until the endstop is triggered, then stop.
 * This is a blocking call (waits for completion).
 */
esp_err_t motor_home(void);

/** Get the queue handle (for direct posting if needed). */
QueueHandle_t motor_get_queue(void);
