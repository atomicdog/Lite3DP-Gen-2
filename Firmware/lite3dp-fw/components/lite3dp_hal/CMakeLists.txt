idf_component_register(
    SRCS
        "src/hal_gpio.c"
        "src/hal_motor.c"
        "src/hal_uv_led.c"
        "src/hal_backlight.c"
        "src/hal_endstop.c"
        "src/hal_buttons.c"
        "src/hal_spi_bus.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)
