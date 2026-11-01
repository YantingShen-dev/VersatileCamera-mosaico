idf_component_register(SRCS "app_board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_fridge
    PRIV_REQUIRES mosaico_module_camera mosaico_module_interact esp-mosaico-bsp
        esp_driver_ppa esp_hw_support esp_mm esp_timer esp_system esp_iris espressif__cjson app_settings app_cloud app_voice
    EMBED_FILES "assets/in.pcm" "assets/out.pcm" "assets/captured.pcm")
