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
    return gpio_get_level(PIN_ENDSTOP) == 0;  /* Active low */
}
