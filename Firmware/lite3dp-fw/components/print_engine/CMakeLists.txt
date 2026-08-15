idf_component_register(
    SRCS
        "src/print_engine.c"
        "src/layer_manager.c"
        "src/png_decoder.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos lite3dp_hal display storage pngdec esp_timer
)
