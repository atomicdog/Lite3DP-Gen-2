idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        lite3dp_hal
        display
        storage
        print_engine
        ui
        webserver
        freertos
        esp_event
        nvs_flash
        esp_timer
)
