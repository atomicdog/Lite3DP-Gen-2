#pragma once

#include "esp_err.h"
#include <stdint.h>

/** Initialize the TFT backlight PWM channel. */
esp_err_t backlight_init(void);

/** Set backlight brightness (0 = off, 255 = full). */
esp_err_t backlight_set(uint8_t brightness);
