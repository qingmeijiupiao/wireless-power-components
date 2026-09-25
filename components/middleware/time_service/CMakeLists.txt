idf_component_register(
    SRCS "src/time_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES diagnostic_log esp_netif esp_event freertos log
)
