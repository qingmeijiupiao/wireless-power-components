idf_component_register(
    SRCS
        "src/blackbox_log_capture.cpp"
        "src/blackbox_service.cpp"
        "src/blackbox_worker.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES blackbox freertos log
)
