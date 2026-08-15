#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t raw_x;
    uint16_t raw_y;
    bool     pressed;
} touch_point_t;

/** Initialize the XPT2046 touch controller over SPI. */
esp_err_t touch_input_init(void);

/**
 * Read the current touch state.
 * Coordinates are mapped to TFT pixel space (0..319 x 0..425).
 */
esp_err_t touch_input_read(touch_point_t *point);

/** Returns true if the touch IRQ pin indicates a touch. */
bool touch_input_pressed(void);
