idf_component_register(SRCS "pj_ui.c" "pj_home_layout.c" "pj_time_model.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lvgl__lvgl)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PJ_UI_USE_LVGL=1)
