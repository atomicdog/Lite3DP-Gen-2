#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t pressure;   /* 0 when not touched; higher = firmer */
    bool     pressed;
} touch_point_t;

/**
 * Unfiltered sample burst, for bring-up and calibration.
 * Nothing here is gated on the IRQ pin or the pressure threshold — this is
 * what the panel actually reports, including when it reports nonsense.
 */
typedef struct {
    uint16_t x_med, x_min, x_max;   /* raw ADC, X channel  */
    uint16_t y_med, y_min, y_max;   /* raw ADC, Y channel  */
    uint16_t z1, z2;                /* raw ADC, Z channels */
    uint16_t pressure;              /* derived from z1/z2  */
    uint8_t  irq;                   /* raw IO35 level      */
    uint16_t mapped_x, mapped_y;    /* x_med/y_med through the current cal */
    uint16_t samples;               /* how many bursts were averaged        */
} touch_raw_t;

/** Runtime-adjustable calibration, so tuning does not cost a flash cycle. */
typedef struct {
    uint16_t x_min, x_max;   /* raw range of the channel driving screen X */
    uint16_t y_min, y_max;   /* raw range of the channel driving screen Y */
    uint16_t screen_w, screen_h;
    bool     swap_xy;        /* raw X channel drives screen Y             */
    bool     invert_x;
    bool     invert_y;
    uint16_t pressure_min;   /* below this, a press is rejected as noise  */
} touch_cal_t;

/** Initialize the XPT2046 touch controller over SPI. */
esp_err_t touch_input_init(void);

/**
 * Read the current touch state, filtered and mapped to LVGL pixel space.
 * `pressed` is true only when the IRQ pin is low *and* the measured pressure
 * clears `pressure_min` — T_IRQ has no pull-up on this board (schematic sheet
 * 2/2, FPC2 pin 11), so the pin alone is not trustworthy.
 */
esp_err_t touch_input_read(touch_point_t *point);

/** Returns true if the touch IRQ pin indicates a touch. Not sufficient alone. */
bool touch_input_pressed(void);

/** Raw sample burst with no gating or filtering. `n` samples, clamped to 1..32. */
esp_err_t touch_input_read_raw(touch_raw_t *out, uint16_t n);

/** Get/set the live calibration. */
void touch_input_get_cal(touch_cal_t *cal);
void touch_input_set_cal(const touch_cal_t *cal);
