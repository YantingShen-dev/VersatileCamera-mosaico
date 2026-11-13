idf_component_register(SRCS "app_voice.cpp" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp-sr esp-mosaico-bsp esp_codec_dev esp_timer)
idf_component_get_property(sr_dir espressif__esp-sr COMPONENT_DIR)
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../tools/pack_voice_model.py"
    "${sr_dir}" "${CMAKE_CURRENT_BINARY_DIR}/photo_srmodels.bin"
    RESULT_VARIABLE pack_result)
if(NOT pack_result EQUAL 0)
    message(FATAL_ERROR "Cannot pack fixed Chinese speech model")
endif()
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_BINARY_DIR}/photo_srmodels.bin" BINARY)
