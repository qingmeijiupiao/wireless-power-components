idf_component_register(
    SRCS "src/espnow_remote.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        battery_level
        blackbox_service
        espnow_link
        espnow_service_remote
        wifi_manager
        esp_timer
        freertos
        log
)
