import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const wasmBytes = await readFile("simulator/generated/pj_ui_wasm.wasm");
const { instance } = await WebAssembly.instantiate(wasmBytes, {
  env: {
    emscripten_resize_heap: () => 0,
  },
});
const api = instance.exports;
const decoder = new TextDecoder();

api.__wasm_call_ctors();

function stateName() {
  const start = api.pj_sim_state_name();
  const bytes = new Uint8Array(api.memory.buffer);
  let end = start;
  while (bytes[end] !== 0) {
    end += 1;
  }
  return decoder.decode(bytes.subarray(start, end));
}

function assertRendered(expectedState) {
  api.pj_sim_render();
  assert.equal(stateName(), expectedState);
  const start = api.pj_sim_framebuffer();
  const end = start + api.pj_sim_framebuffer_bytes();
  const framebuffer = new Uint8Array(api.memory.buffer, start, end - start);
  assert.ok(framebuffer.some((byte) => byte !== 0), `${expectedState} framebuffer is blank`);
}

api.pj_sim_init();
assert.equal(api.pj_sim_display_width(), 200);
assert.equal(api.pj_sim_display_height(), 200);
assert.equal(api.pj_sim_framebuffer_bytes(), 5000);
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
assert.equal(api.pj_sim_record_state(), 1);
api.pj_sim_set_audio_state(1, 0);
assert.equal(api.pj_sim_aux_short(), 1);
assertRendered("home");
assert.equal(api.pj_sim_record_state(), 2);
api.pj_sim_set_audio_state(1, 0);
assert.equal(api.pj_sim_record_state(), 2);
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_record_state(), 0);
assertRendered("home");

api.pj_sim_reset();
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_record_state(), 0);
assertRendered("home");

api.pj_sim_reset();
api.pj_sim_wake();
assertRendered("time_temp");
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
assert.equal(api.pj_sim_aux_double(), 0);
assertRendered("record");

api.pj_sim_reset();
api.pj_sim_wake();
assert.equal(api.pj_sim_aux_short(), 1);
assertRendered("home");
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_aux_short();
api.pj_sim_set_note_count(1);
assert.equal(api.pj_sim_touch_tap(100, 33), 1);
assertRendered("notes");
assert.equal(api.pj_sim_touch_tap(100, 100), 1);
assertRendered("listen");

assert.equal(api.pj_sim_aux_short(), 1);
assert.equal(api.pj_sim_aux_double(), 0);
assertRendered("note_detail");

api.pj_sim_set_audio_state(0, 1);
assert.equal(api.pj_sim_playback_state(), 1);
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_playback_state(), 0);
assertRendered("note_detail");

assert.equal(api.pj_sim_aux_short(), 1);
api.pj_sim_set_audio_state(0, 1);
assert.equal(api.pj_sim_aux_long(), 1);
assert.equal(api.pj_sim_playback_state(), 2);
assertRendered("note_detail");
api.pj_sim_set_audio_state(0, 1);
assert.equal(api.pj_sim_playback_state(), 2);
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_playback_state(), 0);
assertRendered("listen");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_aux_short();
api.pj_sim_set_note_count(1);
api.pj_sim_touch_tap(100, 33);
api.pj_sim_touch_tap(100, 100);
api.pj_sim_touch_tap(100, 95);
api.pj_sim_set_audio_state(0, 1);
assert.equal(api.pj_sim_touch_tap(20, 30), 1);
assert.equal(api.pj_sim_playback_state(), 2);
assertRendered("note_detail");
api.pj_sim_set_audio_state(0, 0);
assertRendered("listen");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_set_status(84, 22, 45);
api.pj_sim_set_time(21, 41, 2026, 6, 6);
api.pj_sim_set_preferences(0, 1, 3);
assertRendered("time_temp");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_aux_short();
assert.equal(api.pj_sim_aux_long(), 1);
assertRendered("time_temp");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_aux_short();
assert.equal(api.pj_sim_touch_tap(20, 125), 1);
assertRendered("time");
assert.equal(api.pj_sim_touch_tap(150, 70), 1);
assertRendered("stopwatch");
assert.equal(api.pj_sim_aux_short(), 1);
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("stopwatch");

console.log("simulator WASM runtime tests passed");
