set(SRCS
    "src/tft_driver.c"
    "src/touch_input.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include"
    REQUIRES driver freertos lite3dp_hal
)
