idf_component_register(SRCS "app_vision.cpp"
    LDFRAGMENTS "coproc_cache_safe.lf"
    INCLUDE_DIRS "include"
    REQUIRES app_face
    PRIV_REQUIRES mosaico_module_camera human_face_detect human_face_recognition
        esp_driver_ppa esp_hw_support esp_timer nvs_flash esp_system)
