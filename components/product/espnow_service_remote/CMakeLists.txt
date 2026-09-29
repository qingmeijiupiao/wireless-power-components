idf_component_register(
    SRCS
        "src/espnow_service_business.cpp"
        "src/espnow_service_business_protocol.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES espnow_link
    PRIV_REQUIRES freertos esp_system
)
