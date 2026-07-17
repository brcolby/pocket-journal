import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

import { quantizeRgbaBuffer, uniqueRgbValues } from "../../simulator/src/display.js";

const frame = new Uint8ClampedArray([
  0, 0, 0, 17,
  255, 255, 255, 4,
  127, 127, 127, 128,
  128, 128, 128, 128,
  60, 180, 60, 128,
]);

quantizeRgbaBuffer(frame);

assert.deepEqual(uniqueRgbValues(frame), [0, 255]);

for (let index = 0; index < frame.length; index += 4) {
  assert.equal(frame[index], frame[index + 1]);
  assert.equal(frame[index], frame[index + 2]);
  assert.equal(frame[index + 3], 255);
  assert.ok(frame[index] === 0 || frame[index] === 255);
}

const glyphAsset = JSON.parse(await readFile("simulator/assets/fonts/carbon-glyphs-1bit.json", "utf8"));
const punctuationAsset = JSON.parse(await readFile("simulator/assets/fonts/ibm-plex-mono-bold-punctuation-1bit.json", "utf8"));
const iconAsset = JSON.parse(await readFile("simulator/assets/icons/carbon-1bit.json", "utf8"));

assert.equal(glyphAsset.family, "Carbon Icons");
assert.equal(glyphAsset.derived_identity_count, 72);
assert.equal(glyphAsset.package.version, "11.82.0");
const glyph = (id, size) => glyphAsset.records.find((record) => record.id === id && record.size === size);
for (const size of [16, 24, 32, 64]) {
  for (const id of ["PJ_CARBON_GLYPH_UPPER_A", "PJ_CARBON_GLYPH_LOWER_A", "PJ_CARBON_GLYPH_DIGIT_1", "PJ_CARBON_GLYPH_DIGIT_9"]) {
    assert.ok(glyph(id, size)?.rows.join("").includes("1"), `${id} ${size}`);
  }
  assert.notDeepEqual(glyph("PJ_CARBON_GLYPH_UPPER_A", size).rows, glyph("PJ_CARBON_GLYPH_LOWER_A", size).rows);
  assert.notDeepEqual(glyph("PJ_CARBON_GLYPH_DIGIT_1", size).rows, glyph("PJ_CARBON_GLYPH_DIGIT_9", size).rows);
}
for (const id of ["PJ_CARBON_GLYPH_SETTINGS_12H", "PJ_CARBON_GLYPH_SETTINGS_24H"]) {
  const composite = glyph(id, 64);
  assert.ok(composite?.rows.join("").includes("1"));
  assert.match(composite.source, /PJ_CARBON_GLYPH_LOWER_H@32/);
  assert.doesNotMatch(composite.source, /PJ_CARBON_GLYPH_UPPER_H/);
}
assert.equal(punctuationAsset.family, "IBM Plex Mono Bold");
assert.equal(punctuationAsset.license, "SIL Open Font License 1.1");
for (const size of [16, 24, 32, 64]) {
  for (const char of [" ", ":", "?", "-"]) {
    assert.ok(punctuationAsset.records.some((record) => record.char === char && record.size === size));
  }
}
assert.equal(iconAsset.family, "Carbon Icons");
assert.equal(iconAsset.record_count, 30);
const icon = (id, size) => iconAsset.records.find((record) => record.id === id && record.size === size);
for (const [id, size] of [
  ["PJ_CARBON_ICON_TIME", 64],
  ["PJ_CARBON_ICON_WAVEFORM", 64],
  ["PJ_CARBON_ICON_PLAY_FILLED", 40],
  ["PJ_CARBON_ICON_PLAY_FILLED", 144],
  ["PJ_CARBON_ICON_BATTERY_HALF", 28],
]) {
  assert.ok(icon(id, size)?.rows.join("").includes("1"), `${id} ${size}`);
}

const simulatorHtml = await readFile("simulator/index.html", "utf8");
const simulatorJs = await readFile("simulator/src/main.js", "utf8");
const simulatorAuxJs = await readFile("simulator/src/aux_input.js", "utf8");
const makefile = await readFile("Makefile", "utf8");
const wasmBridge = await readFile("simulator/wasm/pj_ui_wasm_bridge.c", "utf8");
const appMain = await readFile("firmware/main/app_main.c", "utf8");
const boardSource = await readFile("firmware/components/pj_board/pj_board.c", "utf8");
const galleryTool = await readFile("tools/generate_ui_gallery.mjs", "utf8");
await assert.rejects(
  readFile("simulator/src/ui_model.js", "utf8"),
  (error) => error?.code === "ENOENT",
  "the retired custom-grid simulator model must stay deleted",
);

assert.match(simulatorHtml, /id="powerButton"/);
assert.match(simulatorHtml, /aria-label="Toggle power"/);
assert.match(simulatorHtml, /id="simAuxButton"/);
assert.match(simulatorHtml, /id="simPowerButton"/);
assert.match(simulatorHtml, /id="debugDumpButton"/);
assert.match(simulatorHtml, /id="actionLine"/);
assert.match(simulatorHtml, /id="viewPicker"/);
assert.match(simulatorHtml, /id="captureButton"/);
assert.match(simulatorHtml, /Hardware keys/);
assert.match(simulatorHtml, /module.watchdog.timeout/);
assert.match(simulatorHtml, /__simulator_logs/);
assert.match(simulatorHtml, /simulator-debug-latest\.json/);
assert.match(simulatorHtml, /firmware-wasm-1/);
assert.match(simulatorHtml, /window.addEventListener\("error"/);
assert.match(simulatorHtml, /window.addEventListener\("unhandledrejection"/);
assert.doesNotMatch(simulatorJs, /Material Symbols/);
assert.doesNotMatch(simulatorJs, /ui_model\.js/);
assert.match(simulatorJs, /generated\/pj_ui_wasm\.js/);
assert.match(simulatorJs, /pj_sim_render/);
assert.match(simulatorJs, /pj_sim_dirty_partial/);
assert.match(simulatorJs, /pj_sim_frame_result/);
assert.match(simulatorJs, /pocketJournalSimulator/);
assert.match(simulatorJs, /pocketJournalSimulatorDebugLog/);
assert.match(simulatorJs, /flushDebugLog/);
assert.match(simulatorJs, /postDebugEntries/);
assert.match(simulatorJs, /handlePowerToggle/);
assert.match(simulatorJs, /setAttribute\("aria-pressed"/);
assert.match(simulatorJs, /key === "a"/);
assert.match(simulatorJs, /key === "d"/);
assert.match(simulatorJs, /LONG_PRESS_MS = AUX_LONG_PRESS_MS/);
assert.match(simulatorJs, /DOUBLE_CLICK_MS = AUX_DOUBLE_CLICK_MS/);
assert.match(simulatorJs, /scheduleAuxShort/);
assert.match(simulatorJs, /handleAuxDouble/);
assert.match(simulatorJs, /REVIEW_ROUTES/);
assert.match(simulatorJs, /openReviewView/);
assert.match(simulatorJs, /captureDisplay/);
assert.match(simulatorJs, /event\.shiftKey/);
assert.equal(simulatorJs.match(/!event\.repeat/g)?.length, 5);
assert.match(simulatorAuxJs, /AUX_LONG_PRESS_MS = 500/);
assert.match(simulatorAuxJs, /AUX_DOUBLE_CLICK_MS = 350/);
assert.match(simulatorJs, /putImageData\(framebufferImage, 0, 0, region\.x, region\.y, region\.width, region\.height\)/);
assert.match(simulatorJs, /boot failed; check \.logs\/simulator-debug-latest\.json/);
assert.match(makefile, /generate-simulator-wasm/);
assert.match(makefile, /firmware\/components\/pj_ui\/pj_ui\.c/);
assert.match(makefile, /simulator\/wasm\/pj_ui_wasm_bridge\.c/);
assert.match(makefile, /EXPORTED_FUNCTIONS/);
assert.match(wasmBridge, /pj_ui_presenter_prepare/);
assert.match(wasmBridge, /pj_ui_presenter_accept/);
assert.doesNotMatch(wasmBridge, /pj_ui_render/);
assert.match(wasmBridge, /pj_ui_handle_aux_short/);
assert.match(wasmBridge, /pj_ui_handle_aux_long/);
assert.match(wasmBridge, /pj_ui_handle_aux_double/);
assert.match(wasmBridge, /pj_sim_dirty_partial/);
assert.match(wasmBridge, /pj_sim_frame_result/);
assert.doesNotMatch(makefile, /LVGL|lvgl/);
assert.match(makefile, /pj_ui_presenter\.c/);
assert.match(makefile, /pj_layout_geometry\.c/);
assert.match(appMain, /#include "esp_timer\.h"/);
assert.match(appMain, /esp_timer_get_time\(\)/);
assert.match(appMain, /pj_loop_schedule_init\(&schedule, monotonic_ms\(\)\)/);
assert.match(appMain, /pj_loop_schedule_poll\(\s*&schedule, monotonic_ms\(\)\s*\)/);
assert.match(appMain, /if \(due\.second_due && !g_seconds_cadence\.active\)/);
assert.match(appMain, /service_seconds_cadence\(&g_ui, now_ms\)/);
assert.match(appMain, /pj_display_worker_cadence_start\(1, first_deadline\)/);
assert.match(appMain, /pj_ui_set_recording_elapsed\(ui, status\.recording_elapsed_ms\)/);
assert.match(appMain, /dynamic_changed \|= pj_board_update_time_state\(&g_ui\)/);

assert.match(
  boardSource,
  /#define PJ_AUDIO_CODEC_PA_GAIN_COMPENSATION_DB 0\.0f/,
  "the output route must retain the approved +6 dB gain over the legacy compensation",
);
const audioCodecConfigSource = boardSource.slice(
  boardSource.indexOf("es8311_codec_cfg_t es8311_cfg"),
  boardSource.indexOf("const audio_codec_if_t *codec_if"),
);
assert.match(
  audioCodecConfigSource,
  /\.pa_gain = PJ_AUDIO_CODEC_PA_GAIN_COMPENSATION_DB/,
  "ES8311 setup must use the named zero-dB PA compensation",
);
assert.doesNotMatch(
  audioCodecConfigSource,
  /\.pa_gain = 6\.0f/,
  "the retired 6 dB output attenuation must not return",
);

const displayFullStart = boardSource.indexOf("static esp_err_t epd_refresh_full");
const displayFullSource = boardSource.slice(
  displayFullStart,
  boardSource.indexOf("static int storage_refresh_capacity", displayFullStart),
);
assert.ok(
  displayFullSource.indexOf("epd_send_command(0x26)") <
    displayFullSource.indexOf("epd_send_command(0x24)"),
  "full refresh must seed previous RAM before current RAM and leave 0x24 last",
);
assert.match(boardSource, /ram=0x24->0x26->0x24/);

const cadenceEndSource = appMain.slice(
  appMain.indexOf("static void end_seconds_cadence"),
  appMain.indexOf("static void reconcile_seconds_cadence"),
);
assert.match(cadenceEndSource, /pj_display_worker_cadence_end\(\)/);
assert.match(cadenceEndSource, /if \(cleanup_promoted\)[\s\S]*pj_ui_request_full_presentation\(ui\)/);
assert.match(cadenceEndSource, /\(void\)render_and_submit_if_changed\(ui\)/);
assert.ok(
  cadenceEndSource.indexOf("render_and_submit_if_changed(ui)") >
    cadenceEndSource.indexOf("if (cleanup_promoted)"),
  "cadence exit must always drain pause/reset/navigation after optional cleanup promotion",
);

const recordArmingSource = appMain.slice(
  appMain.indexOf("static int service_record_arming"),
  appMain.indexOf("static int service_seconds_cadence"),
);
assert.match(recordArmingSource, /set_recording_active\(1\)/);
assert.match(recordArmingSource, /reconcile_seconds_cadence\(ui, monotonic_ms\(\)\)/);
assert.ok(
  recordArmingSource.indexOf("set_recording_active(1)") <
    recordArmingSource.indexOf("reconcile_seconds_cadence(ui, monotonic_ms())"),
  "Record cadence must use a fresh monotonic anchor after synchronous capture startup",
);
for (const scenario of [
  "time-temp-battery-${battery}",
  "time-temp-dark",
  "record-arming",
  "record-active",
  "note-detail-playing",
  "transcript-punctuation",
  "transcript-long",
  "stopwatch-running-10",
  "timer-running",
  "timer-paused",
  "interval-running-round-2",
  "volume-minimum",
  "volume-maximum",
  "settings-sync-${phase}",
]) {
  assert.ok(galleryTool.includes(scenario), `gallery is missing ${scenario}`);
}

console.log("simulator one-bit tests passed");
