#pragma once

#include "esp_err.h"
#include <stdint.h>

#define PROFILE_SLOT_COUNT  7   /* 0=active, 1-6=saved (A-F) */

typedef struct {
    float    layer_height;          /* mm (0.025, 0.05, 0.1) */
    float    lift_height;           /* mm */
    float    lift_height_initial;   /* mm (bottom layers) */
    int      bottom_layers;
    int      transition_layers;
    float    exposure_time;         /* seconds */
    int      bottom_exposure;       /* seconds */
    float    lift_speed;            /* mm/s */
    float    lift_speed_initial;    /* mm/s */
    float    retract_speed;         /* mm/s */
    int      rest_time_ms;          /* ms delay after retract */
    int32_t  calibration_offset;    /* additional steps for Z offset */
    uint8_t  uv_power;             /* 0-255 */
} print_profile_t;

/** Initialize the profile store (NVS). */
esp_err_t profile_store_init(void);

/** Load a profile from NVS. Slot 0 = active, 1-6 = saved A-F. */
esp_err_t profile_load(int slot, print_profile_t *profile);

/** Save a profile to NVS. */
esp_err_t profile_save(int slot, const print_profile_t *profile);

/** Fill a profile with factory defaults. */
void profile_defaults(print_profile_t *profile);
