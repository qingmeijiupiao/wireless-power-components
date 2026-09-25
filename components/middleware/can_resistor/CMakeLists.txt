idf_component_register(
    SRCS "src/can_resistor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio HXC_NVS
    PRIV_REQUIRES cpp_gpio_driver log
)
