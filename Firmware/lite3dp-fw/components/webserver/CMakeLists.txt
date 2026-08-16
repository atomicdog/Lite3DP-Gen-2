idf_component_register(
    SRCS
        "src/wifi_manager.c"
        "src/web_server.c"
        "src/web_debug.c"
        "src/web_control.c"
        "src/ota_update.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi esp_http_server esp_event mdns nvs_flash
        app_update json freertos esp_netif esp_timer
        print_engine storage ui lite3dp_hal
)
