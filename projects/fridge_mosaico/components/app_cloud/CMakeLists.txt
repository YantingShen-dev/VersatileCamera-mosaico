idf_component_register(SRCS "app_cloud.cpp" "cloud_asr.cpp" "food_result.c" "audio_pcm.c"
    INCLUDE_DIRS "include"
    REQUIRES app_fridge espressif__cjson
    PRIV_REQUIRES esp_http_client esp-tls mbedtls esp_driver_jpeg esp_netif esp_timer nvs_flash espressif__esp_websocket_client)
