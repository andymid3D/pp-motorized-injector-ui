set(UI_SRCS
    "app_main.cpp"
    "ui_bindings.cpp"
    "uart_display_proto.cpp"
    "display_port.cpp"
    "touch_port.cpp"
)

# Toggle this ON once LVGL/EEZ sources are integrated for ESP-IDF.
set(USE_EEZ_UI OFF)

if(USE_EEZ_UI)
    list(APPEND UI_SRCS
        "../src/ui/actions_impl.cpp"
        "../src/ui/eez-flow.cpp"
        "../src/ui/screens.c"
        "../src/ui/styles.c"
        "../src/ui/ui.c"
        "../src/ui/images.c"
    )
endif()

idf_component_register(
    SRCS ${UI_SRCS}
    INCLUDE_DIRS
        "."
        "../../include"
        "../../src/ui"
)

# LVGL config path (LV_CONF_PATH) points to the lv_conf.h in include/
# This is used by lvgl component to pick up configuration.
add_compile_definitions(LV_CONF_PATH="${CMAKE_CURRENT_LIST_DIR}/../../include/lv_conf.h")
