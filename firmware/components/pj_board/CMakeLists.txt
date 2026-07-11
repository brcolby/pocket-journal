idf_component_register(SRCS "pj_board.c" "pj_aux_input.c" "pj_audio_level.c" "pj_note_model.c" "pj_auth.c" "pj_settings.c" "pj_static_art.c" "pj_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES pj_ui espressif__esp_codec_dev bt cjson mdns esp_adc esp_event esp_http_server esp_netif esp_wifi nvs_flash esp_system esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_sdmmc esp_driver_spi fatfs sdmmc)
