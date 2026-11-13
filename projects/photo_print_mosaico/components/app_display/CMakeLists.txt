idf_component_register(SRCS "app_display.c" "panel_memory.c"
    INCLUDE_DIRS "include"
    REQUIRES esp-gsp
    PRIV_REQUIRES esp-mosaico-bsp esp_lcd esp_display_present log)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_lcd_new_panel_io_spi" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_esp_lcd_new_panel_io_spi" APPEND)
