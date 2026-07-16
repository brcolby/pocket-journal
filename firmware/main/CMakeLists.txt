idf_component_register(SRCS "app_main.c" "pj_display_worker.c" "pj_loop_schedule.c"
                    "pj_sync_inventory_gate.c"
                    INCLUDE_DIRS "."
                    REQUIRES pj_ui pj_board esp_timer freertos)
