#pragma once

#include "esp_err.h"
#include <stdint.h>

/** Initialize the UV LED PWM channel. */
esp_err_t uv_led_init(void);

/** Set UV LED power (0 = off, 255 = full power). */
esp_err_t uv_led_set_power(uint8_t duty);

/** Turn off the UV LED. */
esp_err_t uv_led_off(void);
