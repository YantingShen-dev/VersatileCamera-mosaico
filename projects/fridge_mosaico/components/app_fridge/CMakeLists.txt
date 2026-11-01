idf_component_register(SRCS "app_fridge.c" "fridge_store.c" "fridge_match.c"
    INCLUDE_DIRS "include"
    REQUIRES esp-gsp
    EMBED_FILES "ui/fonts/FridgeSans.ttf")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "ui/main-device.json")
gsp_enable_freetype()
