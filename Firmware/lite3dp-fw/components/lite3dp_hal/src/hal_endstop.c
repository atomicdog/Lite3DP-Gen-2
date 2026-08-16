#include "hal_endstop.h"
#include "hal_gpio.h"
#include "driver/gpio.h"

esp_err_t endstop_init(void)
{
    /* GPIO36 is input-only, already configured in hal_gpio_init */
    return ESP_OK;
}

bool endstop_triggered(void)
{
    /* Active HIGH on this board, measured 2026-08-15: the pin reads low
     * with the platform up and the switch open, and high with the switch
     * pressed. The previous active-low assumption reported "at home"
     * whenever the switch was free, so homing exited before stepping. */
    return gpio_get_level(PIN_ENDSTOP) != 0;
}
