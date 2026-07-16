import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const wasmBytes = await readFile("simulator/generated/pj_ui_wasm.wasm");
const staticArtPbm = await readFile("assets/static/pocket-journal-default.pbm", "utf8");
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

function framebufferSnapshot() {
  api.pj_sim_render();
  const start = api.pj_sim_framebuffer();
  const end = start + api.pj_sim_framebuffer_bytes();
  return new Uint8Array(api.memory.buffer.slice(start, end));
}

function packedPbm(source) {
  const tokens = source.replace(/^#.*$/gm, "").trim().split(/\s+/);
  assert.equal(tokens.shift(), "P1");
  assert.equal(Number(tokens.shift()), 200);
  assert.equal(Number(tokens.shift()), 200);
  assert.equal(tokens.length, 200 * 200);
  const packed = new Uint8Array(5000);
  tokens.forEach((pixel, index) => {
    assert.ok(pixel === "0" || pixel === "1");
    if (pixel === "1") packed[index >> 3] |= 1 << (index & 7);
  });
  return packed;
}

api.pj_sim_init();
assert.equal(api.pj_sim_display_width(), 200);
assert.equal(api.pj_sim_display_height(), 200);
assert.equal(api.pj_sim_framebuffer_bytes(), 5000);
assert.deepEqual(
  framebufferSnapshot(),
  packedPbm(staticArtPbm),
  "simulator fallback must preserve the generated artwork orientation and pixels",
);
assert.equal(api.pj_sim_aux_double(), 0);
assertRendered("static");
api.pj_sim_wake();
assertRendered("home");
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
assert.equal(api.pj_sim_record_state(), 1);
api.pj_sim_set_audio_state(1, 0);
assert.equal(api.pj_sim_aux_long(), 1);
assertRendered("home");
assert.equal(api.pj_sim_record_state(), 2);
api.pj_sim_set_audio_state(1, 0);
assert.equal(api.pj_sim_record_state(), 2);
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_record_state(), 0);
assertRendered("home");

api.pj_sim_reset();
assert.equal(api.pj_sim_aux_double(), 0);
assertRendered("static");
api.pj_sim_wake();
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_record_state(), 0);
assertRendered("home");

api.pj_sim_reset();
api.pj_sim_wake();
assertRendered("home");
assert.equal(api.pj_sim_aux_long(), 1);
assertRendered("time_temp");
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
assert.equal(api.pj_sim_aux_double(), 0);
assertRendered("record");

api.pj_sim_reset();
api.pj_sim_wake();
assertRendered("home");
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");
api.pj_sim_set_audio_state(1, 0);
assert.equal(api.pj_sim_aux_short(), 1);
assert.equal(api.pj_sim_record_state(), 2);
assertRendered("home");
api.pj_sim_set_audio_state(0, 0);

api.pj_sim_reset();
api.pj_sim_wake();
assertRendered("home");
assert.equal(api.pj_sim_aux_double(), 1);
assertRendered("record");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_set_note_count(1);
assert.equal(api.pj_sim_touch_tap(100, 33), 1);
assertRendered("notes");
assert.equal(api.pj_sim_touch_tap(100, 100), 1);
assertRendered("listen");

assert.equal(api.pj_sim_touch_tap(100, 25), 1);
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
assertRendered("listen");
api.pj_sim_set_audio_state(0, 1);
assert.equal(api.pj_sim_playback_state(), 2);
assertRendered("listen");
api.pj_sim_set_audio_state(0, 0);
assert.equal(api.pj_sim_playback_state(), 0);
assertRendered("listen");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_set_note_count(1);
api.pj_sim_touch_tap(100, 33);
api.pj_sim_touch_tap(100, 100);
api.pj_sim_touch_tap(100, 25);
api.pj_sim_set_audio_state(0, 1);
assert.equal(api.pj_sim_touch_tap(20, 30), 1);
assert.equal(api.pj_sim_playback_state(), 2);
assertRendered("note_detail");
assert.equal(api.pj_sim_aux_long(), 1);
assertRendered("listen");
api.pj_sim_set_audio_state(0, 0);
assertRendered("listen");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_seed_review_notes();
api.pj_sim_touch_tap(100, 33);
api.pj_sim_touch_tap(100, 100);
assertRendered("listen");
assert.equal(api.pj_sim_touch_tap(50, 175), 0);
assert.equal(api.pj_sim_touch_tap(150, 175), 1);
assert.equal(api.pj_sim_touch_tap(150, 175), 1);
assert.equal(api.pj_sim_touch_tap(150, 175), 0);
assert.equal(api.pj_sim_touch_tap(50, 175), 1);

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_seed_timestamp_notes();
api.pj_sim_touch_tap(100, 33);
api.pj_sim_touch_tap(100, 100);
assertRendered("listen");

api.pj_sim_reset();
api.pj_sim_wake();
api.pj_sim_set_status(84, 22, 45);
api.pj_sim_set_time(21, 41, 2026, 6, 6);
api.pj_sim_set_preferences(0, 1, 3);
api.pj_sim_aux_long();
assertRendered("time_temp");

api.pj_sim_reset();
api.pj_sim_wake();
assert.equal(api.pj_sim_aux_long(), 1);
assertRendered("time_temp");
const timeTempWithoutIntervalAlert = framebufferSnapshot();
api.pj_sim_set_alert_detail(3, 101, 1);
assert.deepEqual(framebufferSnapshot(), timeTempWithoutIntervalAlert,
  "a recovered interval occurrence must not replace the current screen");
api.pj_sim_set_alert_detail(3, 102, 1);
assert.deepEqual(framebufferSnapshot(), timeTempWithoutIntervalAlert,
  "later interval occurrences must remain nonmodal");
assert.equal(api.pj_sim_aux_short(), 1);
assertRendered("home");

api.pj_sim_reset();
api.pj_sim_wake();
assert.equal(api.pj_sim_aux_long(), 1);
assertRendered("time_temp");

api.pj_sim_reset();
api.pj_sim_wake();
assert.equal(api.pj_sim_touch_tap(20, 125), 1);
assertRendered("time");
assert.equal(api.pj_sim_touch_tap(150, 70), 1);
assertRendered("stopwatch");
assert.equal(api.pj_sim_aux_short(), 1);
assert.equal(api.pj_sim_aux_double(), 0);
assertRendered("stopwatch");

class BrowserEventTarget {
  constructor() {
    this.listeners = new Map();
  }

  addEventListener(type, callback) {
    const callbacks = this.listeners.get(type) ?? [];
    callbacks.push(callback);
    this.listeners.set(type, callbacks);
  }

  emit(type, event = {}) {
    for (const callback of this.listeners.get(type) ?? []) {
      callback(event);
    }
  }
}

function browserElement() {
  const element = new BrowserEventTarget();
  element.style = {};
  element.dataset = {};
  element.textContent = "";
  element.innerHTML = "";
  element.title = "";
  element.value = "";
  element.setAttribute = () => {};
  element.setPointerCapture = () => {};
  element.click = () => {};
  return element;
}

const elements = new Map();
for (const selector of [
  "#display", "#stateName", "#actionLine", "#history", "#powerButton", "#resetButton",
  "#bootButton", "#simAuxButton", "#simPowerButton", "#debugDumpButton", "#viewPicker",
  "#openViewButton", "#captureButton",
]) {
  elements.set(selector, browserElement());
}

const canvas = elements.get("#display");
canvas.width = 200;
canvas.height = 200;
canvas.clientWidth = 200;
canvas.clientHeight = 200;
canvas.closest = () => ({ clientWidth: 448 });
canvas.getBoundingClientRect = () => ({ left: 0, top: 0, width: 200, height: 200 });
canvas.toDataURL = () => "data:image/png;base64,";
canvas.getContext = () => ({
  createImageData: (width, height) => ({ data: new Uint8ClampedArray(width * height * 4) }),
  putImageData: () => {},
});

const browserWindow = new BrowserEventTarget();
browserWindow.innerWidth = 800;
browserWindow.innerHeight = 800;
browserWindow.location = { href: "http://localhost/simulator/index.html" };

const storage = new Map();
Object.defineProperties(globalThis, {
  window: { configurable: true, value: browserWindow },
  document: {
    configurable: true,
    value: {
      querySelector: (selector) => elements.get(selector) ?? null,
      createElement: () => browserElement(),
    },
  },
  localStorage: {
    configurable: true,
    value: {
      getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => storage.set(key, value),
    },
  },
  navigator: { configurable: true, value: { userAgent: "simulator-runtime-test" } },
  HTMLInputElement: { configurable: true, value: class HTMLInputElement {} },
  HTMLTextAreaElement: { configurable: true, value: class HTMLTextAreaElement {} },
});

const wasmArrayBuffer = wasmBytes.buffer.slice(
  wasmBytes.byteOffset,
  wasmBytes.byteOffset + wasmBytes.byteLength,
);
globalThis.fetch = async (resource) => {
  if (String(resource).endsWith("pj_ui_wasm.wasm")) {
    return new Response(wasmArrayBuffer, { headers: { "Content-Type": "application/wasm" } });
  }
  return new Response(null, { status: 204 });
};
globalThis.setInterval = () => 0;
console.debug = () => {};

await import("../../simulator/src/main.js?playback-ack-regression");
for (let attempt = 0; attempt < 100; attempt += 1) {
  if (browserWindow.pocketJournalSimulator?.state() !== "loading") {
    break;
  }
  await new Promise((resolve) => setTimeout(resolve, 10));
}

const simulator = browserWindow.pocketJournalSimulator;
assert.ok(simulator, "browser simulator API was not installed");
assert.equal(simulator.state(), "static");

elements.get("#viewPicker").value = "listen";
elements.get("#openViewButton").emit("click");
assert.equal(simulator.state(), "listen");
simulator.tap(100, 25);
assert.equal(simulator.state(), "note_detail");
simulator.bootShort();
assert.equal(simulator.audioState().playback, 1);
simulator.bootShort();
assert.equal(simulator.audioState().playback, 0, "pause is acknowledged by the simulated board");
simulator.bootLong();
assert.equal(simulator.state(), "listen", "AUX long navigates back after pausing playback");

simulator.tap(100, 25);
assert.equal(simulator.state(), "note_detail");
simulator.bootShort();
assert.equal(simulator.audioState().playback, 1);
simulator.bootLong();
assert.equal(simulator.audioState().playback, 0, "back is acknowledged by the simulated board");
assert.equal(simulator.state(), "listen", "AUX long exits after stopping active playback");

console.log("simulator WASM runtime tests passed");
