#pragma once

#include "esp_err.h"
#include "print_params.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdint.h>

/* Event bits for print_events event group */
#define PRINT_EVT_STARTED     (1 << 0)
#define PRINT_EVT_LAYER_DONE  (1 << 1)
#define PRINT_EVT_PAUSED      (1 << 2)
#define PRINT_EVT_CANCELLED   (1 << 3)
#define PRINT_EVT_FINISHED    (1 << 4)
#define PRINT_EVT_ERROR       (1 << 5)
#define MOTOR_EVT_IDLE        (1 << 6)

typedef enum {
    PRINT_STATE_IDLE,
    PRINT_STATE_CALIBRATING,
    PRINT_STATE_PRINTING_BOTTOM,
    PRINT_STATE_PRINTING_TRANSITION,
    PRINT_STATE_PRINTING_NORMAL,
    PRINT_STATE_PAUSED,
    PRINT_STATE_FINISHING,
    PRINT_STATE_FINISHED,
    PRINT_STATE_ERROR,
    PRINT_STATE_CANCELLED,
} print_state_t;

typedef struct {
    print_state_t state;
    int           current_layer;
    int           total_layers;
    uint32_t      elapsed_ms;
    uint32_t      estimated_remaining_ms;
    char          folder_name[64];
} print_status_t;

/** Initialize the print engine (creates print task, event group). */
esp_err_t print_engine_init(void);

/**
 * Assemble a job from an SD-card folder name: validates the name, builds
 * the path, detects the slicer format, counts layer PNGs and loads the
 * active profile. Shared by the touch UI and the web API so both agree on
 * what a job is — and so folder names from the network get sanitized in
 * exactly one place.
 *
 * @return ESP_ERR_INVALID_ARG  name empty, too long, or contains a path
 *                              separator or ".." traversal
 *         ESP_ERR_NOT_FOUND    no layer PNGs in that folder
 */
esp_err_t print_job_build(const char *folder_name, print_job_t *out);

/** Start a print job. The print task takes ownership of the job data. */
esp_err_t print_start(const print_job_t *job);

/** Pause the current print. */
esp_err_t print_pause(void);

/** Resume a paused print. */
esp_err_t print_resume(void);

/** Cancel the current print. */
esp_err_t print_cancel(void);

/** Get a snapshot of the current print status (thread-safe). */
esp_err_t print_get_status(print_status_t *status);

/** Get the event group for external monitoring. */
EventGroupHandle_t print_get_event_group(void);
