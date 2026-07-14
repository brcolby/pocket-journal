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

const fontAsset = JSON.parse(await readFile("simulator/assets/fonts/space-mono-bold-1bit.json", "utf8"));
const iconAsset = JSON.parse(await readFile("simulator/assets/icons/carbon-1bit.json", "utf8"));

assert.equal(fontAsset.family, "IBM Plex Mono Bold");
assert.equal(fontAsset.source, "assets/fonts/IBMPlexMono-Bold.ttf");
assert.ok(Number.isInteger(fontAsset.sizes["2"].line_height));
assert.ok(fontAsset.sizes["2"].glyphs.A.rows.join("").includes("1"));
assert.ok(fontAsset.sizes["2"].glyphs.Z.rows.join("").includes("1"));
for (const size of Object.values(fontAsset.sizes)) {
  const capitals = [..."ABCDEFGHIJKLMNOPQRSTUVWXYZ"].map((char) => size.glyphs[char]);
  assert.ok(capitals[0].y_offset < size.ascent);
  assert.ok(capitals.every((glyph) => glyph.y_offset === capitals[0].y_offset));
  assert.ok(capitals.every((glyph) => glyph.y_offset + glyph.height <= size.line_height));
}
assert.equal(iconAsset.family, "Carbon Icons");
assert.equal(iconAsset.source, "assets/icons/carbon/svg/32");
for (const iconName of ["notebook", "microphone", "document_audio", "read_me", "settings", "wifi"]) {
  assert.ok(iconAsset.icons[iconName]["58"] || iconAsset.icons[iconName]["40"]);
  assert.ok(iconAsset.icons[iconName]["40"].rows.join("").includes("1"));
}

const simulatorHtml = await readFile("simulator/index.html", "utf8");
const simulatorJs = await readFile("simulator/src/main.js", "utf8");
const simulatorAuxJs = await readFile("simulator/src/aux_input.js", "utf8");
const makefile = await readFile("Makefile", "utf8");
const wasmBridge = await readFile("simulator/wasm/pj_ui_wasm_bridge.c", "utf8");
const appMain = await readFile("firmware/main/app_main.c", "utf8");

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
assert.match(wasmBridge, /pj_ui_render/);
assert.match(wasmBridge, /pj_ui_handle_aux_short/);
assert.match(wasmBridge, /pj_ui_handle_aux_long/);
assert.match(wasmBridge, /pj_ui_handle_aux_double/);
assert.match(wasmBridge, /pj_sim_dirty_partial/);
assert.match(appMain, /PJ_UI_TICKS_PER_SECOND/);
assert.match(appMain, /pj_ui_tick\(&g_ui\)/);
assert.match(appMain, /pj_ui_set_recording_elapsed\(ui, status\.recording_elapsed_ms\)/);
assert.match(appMain, /dynamic_changed \|= pj_board_update_time_state\(&g_ui\)/);

console.log("simulator one-bit tests passed");
