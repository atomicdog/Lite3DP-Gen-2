idf_component_register(
    SRCS
        "src/sd_card.c"
        "src/profile_store.c"
        "src/slicer_detect.c"
    INCLUDE_DIRS "include"
    REQUIRES driver fatfs sdmmc nvs_flash vfs lite3dp_hal
)
