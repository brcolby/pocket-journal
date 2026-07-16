.PHONY: test test-firmware-tools test-ui test-input test-display-worker test-display-pipeline test-partner test-simulator test-simulator-runtime test-ui-images ui-gallery check-lvgl-managed check-static-art generate-static-art generate-font-assets generate-icon-assets generate-simulator-wasm simulator clean

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic
EMCC ?= emcc

UI_TEST_BIN := build/test_ui_core
TIME_MODEL_TEST_BIN := build/test_time_model
AUX_INPUT_TEST_BIN := build/test_aux_input
POWER_INPUT_TEST_BIN := build/test_power_input
AUDIO_LEVEL_TEST_BIN := build/test_audio_level
AUDIO_LIFECYCLE_TEST_BIN := build/test_audio_lifecycle
ALERT_AUDIO_TEST_BIN := build/test_alert_audio
RECORDING_TEST_BIN := build/test_recording
NOTE_MODEL_TEST_BIN := build/test_note_model
AUTH_TEST_BIN := build/test_auth
SETTINGS_TEST_BIN := build/test_settings
HOME_LAYOUT_TEST_BIN := build/test_home_layout
STORAGE_TEST_BIN := build/test_storage
STORAGE_COORDINATOR_TEST_BIN := build/test_storage_coordinator
RUNTIME_DIAGNOSTICS_TEST_BIN := build/test_runtime_diagnostics
LOOP_SCHEDULE_TEST_BIN := build/test_loop_schedule
DISPLAY_WORKER_TEST_BIN := build/test_display_worker
DISPLAY_PIPELINE_TEST_BIN := build/test_display_pipeline
DISPLAY_REFRESH_TEST_BIN := build/test_display_refresh
TIME_CIVIL_TEST_BIN := build/test_time_civil
TIME_CLOCK_TEST_BIN := build/test_time_clock
TIME_TRANSACTION_TEST_BIN := build/test_time_transaction
RTC_WAKE_TEST_BIN := build/test_rtc_wake
TIME_CONTROLLER_TEST_BIN := build/test_time_controller
TRANSCRIPT_UPLOAD_TEST_BIN := build/test_transcript_upload
USB_SYNC_TEST_BIN := build/test_usb_sync
OTA_POLICY_TEST_BIN := build/test_ota_policy
WIFI_STATE_TEST_BIN := build/test_wifi_state
TIME_SYNC_TEST_BIN := build/test_time_sync
COMPANION_SYNC_TEST_BIN := build/test_companion_sync
LVGL_DIR := firmware/managed_components/lvgl__lvgl
ifneq ($(wildcard $(LVGL_DIR)/src),)
LVGL_SRCS := $(wildcard $(LVGL_DIR)/src/*.c)
LVGL_SRCS += $(shell find $(LVGL_DIR)/src/core $(LVGL_DIR)/src/display $(LVGL_DIR)/src/font $(LVGL_DIR)/src/indev $(LVGL_DIR)/src/misc $(LVGL_DIR)/src/tick -name '*.c')
LVGL_SRCS += $(wildcard $(LVGL_DIR)/src/draw/*.c)
LVGL_SRCS += $(shell find $(LVGL_DIR)/src/draw/sw -name '*.c')
LVGL_SRCS += $(wildcard $(LVGL_DIR)/src/libs/bin_decoder/lv_bin_decoder.c)
LVGL_SRCS += $(wildcard $(LVGL_DIR)/src/layouts/*.c $(LVGL_DIR)/src/layouts/flex/*.c)
LVGL_SRCS += $(wildcard $(LVGL_DIR)/src/osal/lv_os.c $(LVGL_DIR)/src/osal/lv_os_none.c)
LVGL_SRCS += $(wildcard $(LVGL_DIR)/src/stdlib/*.c $(LVGL_DIR)/src/stdlib/clib/*.c)
LVGL_SRCS += $(wildcard $(LVGL_DIR)/src/themes/lv_theme.c)
LVGL_SRCS += $(shell find $(LVGL_DIR)/src/widgets/bar $(LVGL_DIR)/src/widgets/button $(LVGL_DIR)/src/widgets/canvas $(LVGL_DIR)/src/widgets/image $(LVGL_DIR)/src/widgets/label $(LVGL_DIR)/src/widgets/line -name '*.c')
endif
LVGL_CFLAGS := -DPJ_UI_USE_LVGL=1 -D_POSIX_C_SOURCE=200809L -DLV_CONF_INCLUDE_SIMPLE -DLV_CONF_SUPPRESS_DEFINE_CHECK -Wno-newline-eof -Wno-unused-parameter -I$(LVGL_DIR) -I$(LVGL_DIR)/src
SIM_WASM_JS := simulator/generated/pj_ui_wasm.js
SIM_WASM := simulator/generated/pj_ui_wasm.wasm
SIM_WASM_EXPORTS := ['_pj_sim_init','_pj_sim_reset','_pj_sim_wake','_pj_sim_sleep','_pj_sim_aux_short','_pj_sim_aux_long','_pj_sim_aux_double','_pj_sim_touch_tap','_pj_sim_tick','_pj_sim_set_status','_pj_sim_set_preferences','_pj_sim_set_time','_pj_sim_set_audio_state','_pj_sim_set_alert','_pj_sim_set_alert_detail','_pj_sim_record_state','_pj_sim_playback_state','_pj_sim_set_note_count','_pj_sim_set_note_label','_pj_sim_seed_review_notes','_pj_sim_seed_timestamp_notes','_pj_sim_render','_pj_sim_framebuffer','_pj_sim_framebuffer_bytes','_pj_sim_display_width','_pj_sim_display_height','_pj_sim_state','_pj_sim_state_name','_pj_sim_dirty_x','_pj_sim_dirty_y','_pj_sim_dirty_width','_pj_sim_dirty_height','_pj_sim_dirty_partial']

test: test-firmware-tools test-ui test-input test-partner test-simulator

test-firmware-tools:
	python3 tests/firmware/test_app_size.py

check-lvgl-managed:
	@if [ ! -d "$(LVGL_DIR)/src" ]; then \
		echo "LVGL managed component is missing. Run: cd firmware && idf.py reconfigure"; \
		exit 1; \
	fi

test-ui: check-lvgl-managed check-static-art
	mkdir -p build
	$(CC) $(CFLAGS) $(LVGL_CFLAGS) \
		-Ifirmware/components/pj_ui/include \
		$(LVGL_SRCS) \
		firmware/components/pj_ui/pj_home_layout.c \
		firmware/components/pj_ui/pj_default_static_art.c \
		firmware/components/pj_ui/pj_ui.c \
		tests/ui/test_ui_core.c \
		-o $(UI_TEST_BIN)
	$(UI_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_ui/pj_time_model.c \
		tests/ui/test_time_model.c \
		-o $(TIME_MODEL_TEST_BIN)
	$(TIME_MODEL_TEST_BIN)

test-display-worker:
	mkdir -p build
	$(CC) $(CFLAGS) \
		-Ifirmware/main \
		-Ifirmware/components/pj_ui/include \
		firmware/main/pj_display_worker.c \
		tests/board/test_display_worker.c \
		-o $(DISPLAY_WORKER_TEST_BIN)
	$(DISPLAY_WORKER_TEST_BIN)

test-display-pipeline: check-static-art
	mkdir -p build
	$(CC) $(CFLAGS) \
		-Ifirmware/main \
		-Ifirmware/components/pj_ui/include \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_ui/pj_home_layout.c \
		firmware/components/pj_ui/pj_default_static_art.c \
		firmware/components/pj_ui/pj_ui.c \
		firmware/components/pj_board/pj_display_refresh.c \
		firmware/main/pj_display_worker.c \
		tests/board/test_display_pipeline.c \
		-o $(DISPLAY_PIPELINE_TEST_BIN)
	$(DISPLAY_PIPELINE_TEST_BIN)

test-input: test-display-worker test-display-pipeline
	mkdir -p build
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_aux_input.c \
		tests/board/test_aux_input.c \
		-o $(AUX_INPUT_TEST_BIN)
	$(AUX_INPUT_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_power_input.c \
		tests/board/test_power_input.c \
		-o $(POWER_INPUT_TEST_BIN)
	$(POWER_INPUT_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_audio_level.c \
		tests/board/test_audio_level.c \
		-o $(AUDIO_LEVEL_TEST_BIN)
	$(AUDIO_LEVEL_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_audio_lifecycle.c \
		tests/board/test_audio_lifecycle.c \
		-o $(AUDIO_LIFECYCLE_TEST_BIN)
	$(AUDIO_LIFECYCLE_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_alert_audio.c \
		tests/board/test_alert_audio.c \
		-o $(ALERT_AUDIO_TEST_BIN)
	$(ALERT_AUDIO_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_recording.c \
		tests/board/test_recording.c \
		-o $(RECORDING_TEST_BIN)
	$(RECORDING_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_note_model.c \
		tests/board/test_note_model.c \
		-o $(NOTE_MODEL_TEST_BIN)
	$(NOTE_MODEL_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_auth.c \
		tests/board/test_auth.c \
		-o $(AUTH_TEST_BIN)
	$(AUTH_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_settings.c \
		tests/board/test_settings.c \
		-o $(SETTINGS_TEST_BIN)
	$(SETTINGS_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_ui/pj_home_layout.c \
		tests/board/test_home_layout.c \
		-o $(HOME_LAYOUT_TEST_BIN)
	$(HOME_LAYOUT_TEST_BIN)
	$(CC) $(CFLAGS) \
		-D_POSIX_C_SOURCE=200809L \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_storage.c \
		tests/board/test_storage.c \
		-o $(STORAGE_TEST_BIN)
	$(STORAGE_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_storage_coordinator.c \
		tests/board/test_storage_coordinator.c \
		-o $(STORAGE_COORDINATOR_TEST_BIN)
	$(STORAGE_COORDINATOR_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_runtime_diagnostics.c \
		tests/board/test_runtime_diagnostics.c \
		-o $(RUNTIME_DIAGNOSTICS_TEST_BIN)
	$(RUNTIME_DIAGNOSTICS_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/main \
		firmware/main/pj_loop_schedule.c \
		tests/board/test_loop_schedule.c \
		-o $(LOOP_SCHEDULE_TEST_BIN)
	$(LOOP_SCHEDULE_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_board/pj_display_refresh.c \
		tests/board/test_display_refresh.c \
		-o $(DISPLAY_REFRESH_TEST_BIN)
	$(DISPLAY_REFRESH_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_time_civil.c \
		tests/board/test_time_civil.c \
		-o $(TIME_CIVIL_TEST_BIN)
	$(TIME_CIVIL_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_ui/pj_time_model.c \
		firmware/components/pj_board/pj_time_clock.c \
		tests/board/test_time_clock.c \
		-o $(TIME_CLOCK_TEST_BIN)
	$(TIME_CLOCK_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_time_transaction.c \
		tests/board/test_time_transaction.c \
		-o $(TIME_TRANSACTION_TEST_BIN)
	$(TIME_TRANSACTION_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_ui/pj_time_model.c \
		firmware/components/pj_board/pj_time_clock.c \
		firmware/components/pj_board/pj_rtc_wake.c \
		tests/board/test_rtc_wake.c \
		-o $(RTC_WAKE_TEST_BIN)
	$(RTC_WAKE_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_ui/pj_time_model.c \
		firmware/components/pj_board/pj_time_controller.c \
		tests/board/test_time_controller.c \
		-o $(TIME_CONTROLLER_TEST_BIN)
	$(TIME_CONTROLLER_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		-Ifirmware/managed_components/espressif__cjson/cJSON \
		firmware/managed_components/espressif__cjson/cJSON/cJSON.c \
		firmware/components/pj_board/pj_transcript_upload.c \
		tests/board/test_transcript_upload.c \
		-lm \
		-o $(TRANSCRIPT_UPLOAD_TEST_BIN)
	$(TRANSCRIPT_UPLOAD_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_usb_sync.c \
		tests/board/test_usb_sync.c \
		-o $(USB_SYNC_TEST_BIN)
	$(USB_SYNC_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_ota_policy.c \
		tests/board/test_ota_policy.c \
		-o $(OTA_POLICY_TEST_BIN)
	$(OTA_POLICY_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_wifi_state.c \
		tests/board/test_wifi_state.c \
		-o $(WIFI_STATE_TEST_BIN)
	$(WIFI_STATE_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_time_sync.c \
		tests/board/test_time_sync.c \
		-o $(TIME_SYNC_TEST_BIN)
	$(TIME_SYNC_TEST_BIN)
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_board/include \
		firmware/components/pj_board/pj_companion_sync.c \
		tests/board/test_companion_sync.c \
		-o $(COMPANION_SYNC_TEST_BIN)
	$(COMPANION_SYNC_TEST_BIN)

test-partner:
	cd partner && PYTHONPATH=src python -m unittest discover -s ../tests/partner -p 'test_*.py'

test-simulator:
	node tests/simulator/test_one_bit.mjs
	node tests/simulator/test_aux_input.mjs

test-simulator-runtime: generate-simulator-wasm
	node tests/simulator/test_static_art_render.mjs
	node tests/simulator/test_wasm_runtime.mjs

test-ui-images:
	node tests/simulator/test_ui_image_checks.mjs

ui-gallery: generate-simulator-wasm test-ui-images
	node tools/generate_ui_gallery.mjs

generate-font-assets:
	python3 tools/generate_font_assets.py

generate-icon-assets:
	python3 tools/generate_icon_assets.py

generate-static-art:
	python3 tools/generate_static_art.py

check-static-art:
	python3 tools/generate_static_art.py --check

generate-simulator-wasm: check-lvgl-managed check-static-art
	@if ! command -v $(EMCC) >/dev/null 2>&1; then \
		if [ -f "$(SIM_WASM_JS)" ] && [ -f "$(SIM_WASM)" ]; then \
			echo "emcc not found; using existing $(SIM_WASM_JS)."; \
			exit 0; \
		fi; \
		echo "emcc is required to build the firmware-backed simulator WASM, and no generated artifact exists."; \
		echo "Install/activate Emscripten, then run: make generate-simulator-wasm"; \
		exit 1; \
	fi
	mkdir -p simulator/generated
	$(EMCC) -std=c11 -O2 $(LVGL_CFLAGS) \
		-Ifirmware/components/pj_ui/include \
		$(LVGL_SRCS) \
		firmware/components/pj_ui/pj_home_layout.c \
		firmware/components/pj_ui/pj_default_static_art.c \
		firmware/components/pj_ui/pj_ui.c \
		simulator/wasm/pj_ui_wasm_bridge.c \
		-o $(SIM_WASM_JS) \
		-s MODULARIZE=1 \
		-s EXPORT_ES6=1 \
		-s ENVIRONMENT=web \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s EXPORTED_FUNCTIONS="$(SIM_WASM_EXPORTS)" \
		-s EXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString","HEAPU8"]'

simulator: generate-simulator-wasm
	python3 tools/simulator_server.py

clean:
	rm -rf build firmware/build partner/build partner/dist partner/*.egg-info simulator/generated
