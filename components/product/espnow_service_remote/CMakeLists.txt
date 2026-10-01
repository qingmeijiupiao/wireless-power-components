idf_component_register(
    SRCS "src/espnow_service_business.cpp"
    INCLUDE_DIRS "include"
    REQUIRES espnow_link espnow_service_proto
    PRIV_REQUIRES freertos esp_system
)
