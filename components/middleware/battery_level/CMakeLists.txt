idf_component_register(
    SRCS "src/battery_level.cpp"
    INCLUDE_DIRS "include"
    REQUIRES Interp esp_common esp_hw_support
)
