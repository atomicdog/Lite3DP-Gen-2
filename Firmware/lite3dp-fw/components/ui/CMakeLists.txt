idf_component_register(
    SRCS
        "src/ui_manager.c"
        "src/ui_screens.c"
        "src/input_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos lite3dp_hal display storage print_engine lvgl
)
