idf_component_register(
                    SRCS "src/HXC_NVS.cpp" 
                    REQUIRES nvs_flash
                    PRIV_REQUIRES freertos log
                    INCLUDE_DIRS "include")
