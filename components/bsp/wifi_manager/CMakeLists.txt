idf_component_register(
                    SRCS "src/wifi_manager.cpp"
                    REQUIRES esp_wifi esp_netif esp_event lwip
                    PRIV_REQUIRES freertos log
                    INCLUDE_DIRS "include")
