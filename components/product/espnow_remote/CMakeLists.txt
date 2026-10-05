idf_component_register(
    SRCS "src/espnow_remote.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        battery_level
        espnow_link
        espnow_service_remote
        wifi_manager
        esp_timer
        freertos
        log
)
