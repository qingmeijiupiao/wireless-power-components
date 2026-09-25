idf_component_register(
    SRCS "src/nvs_gpio_output.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES HXC_NVS cpp_gpio_driver log
)
