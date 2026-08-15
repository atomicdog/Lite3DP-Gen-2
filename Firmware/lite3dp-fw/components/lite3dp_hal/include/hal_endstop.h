#pragma once

#include "esp_err.h"
#include <stdbool.h>

/** Initialize the endstop input pin. */
esp_err_t endstop_init(void);

/** Returns true when the endstop switch is triggered (active low). */
bool endstop_triggered(void);
