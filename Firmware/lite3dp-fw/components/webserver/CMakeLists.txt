idf_component_register(
    SRCS
        "src/wifi_manager.c"
        "src/web_server.c"
        "src/ota_update.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi esp_http_server esp_event mdns nvs_flash
        app_update json freertos esp_netif
        print_engine storage
)
