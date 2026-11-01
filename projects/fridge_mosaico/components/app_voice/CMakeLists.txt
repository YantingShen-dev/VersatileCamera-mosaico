idf_component_register(SRCS "app_voice.cpp" INCLUDE_DIRS "include"
    REQUIRES app_fridge app_cloud
    PRIV_REQUIRES esp-mosaico-bsp espressif__esp-sr esp_partition esp_timer)
idf_component_get_property(esp_sr_path espressif__esp-sr COMPONENT_DIR)
set(voice_image "${esp_sr_path}/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaole.dat")
add_custom_target(fridge_voice_data ALL DEPENDS "${voice_image}")
set_property(GLOBAL APPEND PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_LABELS voice_data)
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_voice_data_IMAGE "${voice_image}")
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_voice_data_TARGET fridge_voice_data)
esptool_py_flash_to_partition(flash "voice_data" "${voice_image}")
