idf_component_register(
                    SRCS "src/HXC_TWAI.cpp" 
                    REQUIRES esp_driver_twai
                    PRIV_REQUIRES freertos log
                    INCLUDE_DIRS "include")
