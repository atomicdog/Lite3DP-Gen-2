#include "print_engine.h"
#include "layer_manager.h"
#include "png_decoder.h"
#include "hal_motor.h"
#include "hal_uv_led.h"
#include "hal_gpio.h"
#include "hal_spi_bus.h"
#include "sd_card.h"
#include "tft_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "print_engine";

#define PRINT_TASK_STACK    8192
#define PRINT_TASK_PRIO     5
#define PRINT_TASK_CORE     1

/* ── Shared state ──────────────────────────────────────────────── */

static EventGroupHandle_t  s_print_events;
static SemaphoreHandle_t   s_status_mutex;
static print_status_t      s_status;
static print_job_t         s_job;
static TaskHandle_t        s_task_handle;
static bool                s_pause_requested;
static bool                s_cancel_requested;

/* ── Helpers ───────────────────────────────────────────────────── */

static void update_status(print_state_t state, int layer)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.state = state;
    s_status.current_layer = layer;
    xSemaphoreGive(s_status_mutex);
}

static void wait_motor_idle(void)
{
    xEventGroupWaitBits(s_print_events, MOTOR_EVT_IDLE, pdTRUE, pdTRUE, portMAX_DELAY);
}

static float calc_retract_height(float lift_height, float layer_height)
{
    /* Retract = lift - one layer (net rise per layer = layer_height) */
    return lift_height - layer_height;
}

/* ── Print task ────────────────────────────────────────────────── */

static void print_task(void *arg)
{
    for (;;) {
        /* Wait for a print start signal */
        xEventGroupWaitBits(s_print_events, PRINT_EVT_STARTED, pdTRUE, pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Print starting: %s (%d layers)", s_job.folder_name, s_job.total_layers);

        const print_profile_t *p = &s_job.profile;
        uint32_t start_tick = xTaskGetTickCount();
        s_pause_requested = false;
        s_cancel_requested = false;

        /* ── Calibration: home to endstop ──────────────────────── */
        update_status(PRINT_STATE_CALIBRATING, 0);

        motor_home();

        /* Apply calibration offset */
        if (p->calibration_offset > 0) {
            uint32_t delay_us = (uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * 1.0f));
            motor_move_steps(MOTOR_DIR_UP, (uint32_t)p->calibration_offset, delay_us);
            wait_motor_idle();
        }

        /* Move up one layer height to start position */
        motor_move_mm(p->layer_height, p->lift_speed);
        wait_motor_idle();

        /* ── Layer loop ────────────────────────────────────────── */
        bool aborted = false;

        for (int layer = 0; layer < s_job.total_layers && !aborted; layer++) {
            /* Determine print phase */
            print_state_t phase;
            if (layer < p->bottom_layers) {
                phase = PRINT_STATE_PRINTING_BOTTOM;
            } else if (layer < p->bottom_layers + p->transition_layers) {
                phase = PRINT_STATE_PRINTING_TRANSITION;
            } else {
                phase = PRINT_STATE_PRINTING_NORMAL;
            }
            update_status(phase, layer);

            /* Calculate layer parameters */
            float expo_time = layer_exposure_time(
                layer, p->bottom_layers, p->transition_layers,
                (float)p->bottom_exposure, p->exposure_time
            );
            float lift_h = layer_lift_height(
                layer, p->bottom_layers, p->transition_layers,
                p->lift_height_initial, p->lift_height
            );
            float lift_spd = layer_lift_speed(
                layer, p->bottom_layers, p->transition_layers,
                p->lift_speed_initial, p->lift_speed
            );
            float retract_h = calc_retract_height(lift_h, p->layer_height);

            /* 1. Rest time */
            if (p->rest_time_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(p->rest_time_ms));
            }

            /* 2. Decode and display layer image on TFT (needs SPI bus) */
            int img_idx = layer_image_index(layer, p->layer_height);
            char layer_path[SD_MAX_PATH];
            slicer_get_layer_path(s_job.slicer, s_job.folder_path,
                                  s_job.folder_name, img_idx,
                                  layer_path, sizeof(layer_path));

            spi_bus_acquire();
            tft_set_rotation(2);  /* Print orientation */
            esp_err_t dec_ret = png_decode_to_tft(layer_path);
            spi_bus_release();

            if (dec_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to decode layer %d: %s", layer, layer_path);
                update_status(PRINT_STATE_ERROR, layer);
                xEventGroupSetBits(s_print_events, PRINT_EVT_ERROR);
                aborted = true;
                break;
            }

            /* 3. UV exposure */
            uv_led_set_power(p->uv_power);
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(expo_time * 1000.0f)));
            uv_led_off();

            /* 4. Clear TFT */
            spi_bus_acquire();
            tft_fill_screen(0x0000);
            spi_bus_release();

            /* 5. Check pause */
            if (s_pause_requested) {
                update_status(PRINT_STATE_PAUSED, layer);
                xEventGroupSetBits(s_print_events, PRINT_EVT_PAUSED);

                /* Wait for resume or cancel */
                while (s_pause_requested && !s_cancel_requested) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                if (s_cancel_requested) {
                    aborted = true;
                    break;
                }
            }

            if (s_cancel_requested) {
                aborted = true;
                break;
            }

            /* 6. Lift */
            uint32_t lift_delay = (uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * lift_spd));
            uint32_t lift_steps = (uint32_t)roundf(lift_h * MOTOR_STEPS_PER_MM);
            motor_move_steps(MOTOR_DIR_UP, lift_steps, lift_delay);
            wait_motor_idle();

            /* 7. Retract */
            uint32_t retract_delay = (uint32_t)(1000000.0f / (MOTOR_STEPS_PER_MM * p->retract_speed));
            uint32_t retract_steps = (uint32_t)roundf(retract_h * MOTOR_STEPS_PER_MM);
            motor_move_steps(MOTOR_DIR_DOWN, retract_steps, retract_delay);
            wait_motor_idle();

            /* 8. Update status */
            uint32_t elapsed_ms = (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
            uint32_t avg_layer_ms = (layer > 0) ? elapsed_ms / layer : 0;
            uint32_t remaining_ms = avg_layer_ms * (s_job.total_layers - layer - 1);

            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
            s_status.elapsed_ms = elapsed_ms;
            s_status.estimated_remaining_ms = remaining_ms;
            xSemaphoreGive(s_status_mutex);

            xEventGroupSetBits(s_print_events, PRINT_EVT_LAYER_DONE);

            ESP_LOGI(TAG, "Layer %d/%d done (expo=%.1fs, lift=%.1fmm)",
                     layer + 1, s_job.total_layers, (double)expo_time, (double)lift_h);
        }

        /* ── Finish or cancel ──────────────────────────────────── */
        if (aborted && s_cancel_requested) {
            update_status(PRINT_STATE_CANCELLED, s_status.current_layer);
            xEventGroupSetBits(s_print_events, PRINT_EVT_CANCELLED);
            ESP_LOGI(TAG, "Print cancelled at layer %d", s_status.current_layer);
        } else if (!aborted) {
            /* Lift platform to top for part removal */
            update_status(PRINT_STATE_FINISHING, s_job.total_layers);
            motor_move_mm(50.0f, p->lift_speed);
            wait_motor_idle();

            uint32_t total_ms = (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
            s_status.elapsed_ms = total_ms;
            s_status.estimated_remaining_ms = 0;
            xSemaphoreGive(s_status_mutex);

            update_status(PRINT_STATE_FINISHED, s_job.total_layers);
            xEventGroupSetBits(s_print_events, PRINT_EVT_FINISHED);

            ESP_LOGI(TAG, "Print complete: %d layers in %lu seconds",
                     s_job.total_layers, (unsigned long)(total_ms / 1000));
        }

        motor_disable();

        /* Return to menu rotation */
        tft_set_rotation(3);
    }
}

/* ── Public API ────────────────────────────────────��───────────── */

esp_err_t print_engine_init(void)
{
    s_print_events = xEventGroupCreate();
    if (!s_print_events) return ESP_ERR_NO_MEM;

    s_status_mutex = xSemaphoreCreateMutex();
    if (!s_status_mutex) return ESP_ERR_NO_MEM;

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = PRINT_STATE_IDLE;

    /* Initialize motor with our event group */
    esp_err_t ret = motor_init(s_print_events, MOTOR_EVT_IDLE);
    if (ret != ESP_OK) return ret;

    BaseType_t xret = xTaskCreatePinnedToCore(
        print_task, "print", PRINT_TASK_STACK, NULL,
        PRINT_TASK_PRIO, &s_task_handle, PRINT_TASK_CORE
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create print task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Print engine initialized");
    return ESP_OK;
}

esp_err_t print_start(const print_job_t *job)
{
    if (s_status.state != PRINT_STATE_IDLE &&
        s_status.state != PRINT_STATE_FINISHED &&
        s_status.state != PRINT_STATE_CANCELLED &&
        s_status.state != PRINT_STATE_ERROR) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(&s_job, job, sizeof(print_job_t));
    strncpy(s_status.folder_name, job->folder_name, sizeof(s_status.folder_name) - 1);
    s_status.total_layers = job->total_layers;
    s_status.current_layer = 0;
    s_status.elapsed_ms = 0;
    s_status.estimated_remaining_ms = 0;

    /* Signal the print task to start */
    xEventGroupSetBits(s_print_events, PRINT_EVT_STARTED);
    return ESP_OK;
}

esp_err_t print_pause(void)
{
    if (s_status.state != PRINT_STATE_PRINTING_BOTTOM &&
        s_status.state != PRINT_STATE_PRINTING_TRANSITION &&
        s_status.state != PRINT_STATE_PRINTING_NORMAL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pause_requested = true;
    return ESP_OK;
}

esp_err_t print_resume(void)
{
    if (s_status.state != PRINT_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pause_requested = false;
    return ESP_OK;
}

esp_err_t print_cancel(void)
{
    s_cancel_requested = true;
    s_pause_requested = false;  /* Unblock if paused */
    return ESP_OK;
}

esp_err_t print_get_status(print_status_t *status)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    memcpy(status, &s_status, sizeof(print_status_t));
    xSemaphoreGive(s_status_mutex);
    return ESP_OK;
}

EventGroupHandle_t print_get_event_group(void)
{
    return s_print_events;
}
