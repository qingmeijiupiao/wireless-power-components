idf_component_register(SRCS "src/shell.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES console esp_timer vfs esp_driver_usb_serial_jtag freertos esp_usb_cdc_rom_console)
