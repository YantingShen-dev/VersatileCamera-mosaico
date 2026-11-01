idf_component_register(SRCS "app_settings.cpp" INCLUDE_DIRS "include"
    REQUIRES app_fridge PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer)
