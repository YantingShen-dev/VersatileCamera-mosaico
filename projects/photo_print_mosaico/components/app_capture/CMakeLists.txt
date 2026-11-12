idf_component_register(SRCS "app_capture.cpp" INCLUDE_DIRS "include"
    LDFRAGMENTS "coproc_cache_safe.lf" REQUIRES app_photo
    PRIV_REQUIRES app_printer mosaico_module_camera esp_driver_ppa esp_hw_support esp_timer esp_system)
