idf_component_register(
    SRCS "src/espnow_service_proto.cpp" "src/remote_switch_registry.cpp"
    INCLUDE_DIRS "include"
    REQUIRES espnow_link
    PRIV_REQUIRES esp_timer freertos log
)
