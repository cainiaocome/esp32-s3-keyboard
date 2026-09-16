idf_component_register(
    SRCS
        "app_config.cpp"
        "app_main.cpp"
        "http_server.cpp"
        "wifi_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        cjson
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
        usb_hid
        keyboard_core
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/Kconfig.projbuild"
)
