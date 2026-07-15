import {
  AUX_DOUBLE_CLICK_MS,
  AUX_LONG_PRESS_MS,
  createAuxClickDetector,
  createAuxPressDetector,
} from "./aux_input.js";

const canvas = document.querySelector("#display");
const ctx = canvas.getContext("2d", { alpha: false });
const stateName = document.querySelector("#stateName");
const actionLine = document.querySelector("#actionLine");
const history = document.querySelector("#history");
const powerButton = document.querySelector("#powerButton");
const resetButton = document.querySelector("#resetButton");
const bootButton = document.querySelector("#bootButton");
const simAuxButton = document.querySelector("#simAuxButton");
const simPowerButton = document.querySelector("#simPowerButton");
const debugDumpButton = document.querySelector("#debugDumpButton");
const viewPicker = document.querySelector("#viewPicker");
const openViewButton = document.querySelector("#openViewButton");
const captureButton = document.querySelector("#captureButton");

const WASM_MODULE_URL = "../generated/pj_ui_wasm.js?v=firmware-wasm-1";
const DEBUG_LOG_KEY = "pocketJournalSimulatorDebugLog";
const DEBUG_LOG_LIMIT = 500;
const DISPLAY_WIDTH = 200;
const DISPLAY_HEIGHT = 200;
const LONG_PRESS_MS = AUX_LONG_PRESS_MS;
const DOUBLE_CLICK_MS = AUX_DOUBLE_CLICK_MS;
const PLAYBACK_STOPPING = 2;

let wasmModule = null;
let api = null;
let framebufferImage = ctx.createImageData(DISPLAY_WIDTH, DISPLAY_HEIGHT);
let auxPressPointerId = null;
const visited = [];
const debugLog = loadDebugLog();

const REVIEW_ROUTES = {
  static: [],
  time_temp: ["wake", "back"],
  home: ["wake"],
  notes: ["wake", [100, 33]],
  record: ["wake", [100, 33], [100, 33]],
  listen: ["wake", [100, 33], [100, 100]],
  listen_page_2: ["wake", [100, 33], [100, 100], [150, 175]],
  read: ["wake", [100, 33], [100, 166]],
  time: ["wake", [20, 125]],
  alarm: ["wake", [20, 125], [40, 70]],
  stopwatch: ["wake", [20, 125], [150, 70]],
  timer: ["wake", [20, 125], [40, 150]],
  interval: ["wake", [20, 125], [150, 150]],
  settings: ["wake", [20, 170]],
  volume: ["wake", [20, 170], [50, 50]],
  sync: ["wake", [20, 170], [150, 150]],
};

window.__pocketJournalSimulatorModuleLoaded = true;

function safeError(error) {
  if (!error) {
    return null;
  }
  return {
    name: error.name ?? "Error",
    message: error.message ?? String(error),
    stack: error.stack ?? null,
  };
}

function stateLabel() {
  if (!api) {
    return "loading";
  }
  return wasmModule.UTF8ToString(api.stateName());
}

function dirtyRegion() {
  if (!api) {
    return { x: 0, y: 0, width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT, partial: 0 };
  }
  return {
    x: api.dirtyX(),
    y: api.dirtyY(),
    width: api.dirtyWidth(),
    height: api.dirtyHeight(),
    partial: api.dirtyPartial(),
  };
}

function simulatorSnapshot(extra = {}) {
  return {
    state: stateLabel(),
    dirty: dirtyRegion(),
    wasmLoaded: Boolean(wasmModule),
    canvas: canvas
      ? {
          width: canvas.width,
          height: canvas.height,
          clientWidth: canvas.clientWidth,
          clientHeight: canvas.clientHeight,
        }
      : null,
    location: window.location.href,
    userAgent: navigator.userAgent,
    ...extra,
  };
}

function loadDebugLog() {
  try {
    const raw = localStorage.getItem(DEBUG_LOG_KEY);
    if (!raw) {
      return [];
    }
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed.slice(-DEBUG_LOG_LIMIT) : [];
  } catch (error) {
    console.warn("Unable to load simulator debug log", error);
    return [];
  }
}

function persistDebugLog() {
  try {
    localStorage.setItem(DEBUG_LOG_KEY, JSON.stringify(debugLog.slice(-DEBUG_LOG_LIMIT)));
  } catch (error) {
    console.warn("Unable to persist simulator debug log", error);
  }
}

function postDebugEntries(entries, reason = "module") {
  if (typeof window.__pocketJournalSimulatorPersistEntries === "function") {
    window.__pocketJournalSimulatorPersistEntries(entries, reason);
    return;
  }
  const body = JSON.stringify({
    reason,
    generatedAt: new Date().toISOString(),
    href: window.location.href,
    userAgent: navigator.userAgent,
    entries,
  });
  fetch("/__simulator_logs", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body,
    keepalive: true,
  }).catch((error) => {
    console.warn("Unable to persist simulator debug log to .logs/", error);
  });
}

function traceDebug(event, details = {}) {
  const entry = {
    sequence: debugLog.length + 1,
    time: new Date().toISOString(),
    event,
    details,
    snapshot: simulatorSnapshot(),
  };
  debugLog.push(entry);
  if (debugLog.length > DEBUG_LOG_LIMIT) {
    debugLog.splice(0, debugLog.length - DEBUG_LOG_LIMIT);
  }
  persistDebugLog();
  postDebugEntries([entry], "module");
  console.debug("[Pocket Journal Simulator]", event, details, entry.snapshot);
  return entry;
}

function debugDump() {
  return {
    generatedAt: new Date().toISOString(),
    key: DEBUG_LOG_KEY,
    entries: debugLog.slice(),
    snapshot: simulatorSnapshot(),
  };
}

function flushDebugLog() {
  postDebugEntries(debugLog.slice(), "manual_flush");
  traceDebug("debug.flush", { entries: debugLog.length });
}

function clearDebugLog() {
  debugLog.splice(0, debugLog.length);
  persistDebugLog();
  traceDebug("debug.clear");
}

function logAction(message) {
  actionLine.textContent = message;
  traceDebug("action", { message });
}

function updateDisplayScale() {
  const device = canvas.closest(".device-stage");
  const availableWidth = Math.max(DISPLAY_WIDTH, (device?.clientWidth ?? window.innerWidth) - 48);
  const availableHeight = Math.max(DISPLAY_HEIGHT, window.innerHeight - 220);
  const scale = Math.max(1, Math.min(4, Math.floor(Math.min(availableWidth, availableHeight) / DISPLAY_WIDTH)));
  canvas.style.width = `${DISPLAY_WIDTH * scale}px`;
  canvas.style.height = `${DISPLAY_HEIGHT * scale}px`;
  traceDebug("display.scale", {
    availableWidth,
    availableHeight,
    scale,
    styleWidth: canvas.style.width,
    styleHeight: canvas.style.height,
  });
}

function bindApi(module) {
  return {
    init: module.cwrap("pj_sim_init", null, []),
    reset: module.cwrap("pj_sim_reset", null, []),
    wake: module.cwrap("pj_sim_wake", null, []),
    sleep: module.cwrap("pj_sim_sleep", null, []),
    auxShort: module.cwrap("pj_sim_aux_short", "number", []),
    auxLong: module.cwrap("pj_sim_aux_long", "number", []),
    auxDouble: module.cwrap("pj_sim_aux_double", "number", []),
    touchTap: module.cwrap("pj_sim_touch_tap", "number", ["number", "number"]),
    tick: module.cwrap("pj_sim_tick", "number", []),
    setStatus: module.cwrap("pj_sim_set_status", null, ["number", "number", "number"]),
    setPreferences: module.cwrap("pj_sim_set_preferences", null, ["number", "number", "number"]),
    setTime: module.cwrap("pj_sim_set_time", null, ["number", "number", "number", "number", "number"]),
    setAudioState: module.cwrap("pj_sim_set_audio_state", null, ["number", "number"]),
    setAlert: module.cwrap("pj_sim_set_alert", null, ["number"]),
    recordState: module.cwrap("pj_sim_record_state", "number", []),
    playbackState: module.cwrap("pj_sim_playback_state", "number", []),
    setNoteCount: module.cwrap("pj_sim_set_note_count", null, ["number"]),
    setNoteLabel: module.cwrap("pj_sim_set_note_label", null, ["number", "string"]),
    seedReviewNotes: module.cwrap("pj_sim_seed_review_notes", null, []),
    render: module.cwrap("pj_sim_render", null, []),
    framebuffer: module.cwrap("pj_sim_framebuffer", "number", []),
    framebufferBytes: module.cwrap("pj_sim_framebuffer_bytes", "number", []),
    displayWidth: module.cwrap("pj_sim_display_width", "number", []),
    displayHeight: module.cwrap("pj_sim_display_height", "number", []),
    state: module.cwrap("pj_sim_state", "number", []),
    stateName: module.cwrap("pj_sim_state_name", "number", []),
    dirtyX: module.cwrap("pj_sim_dirty_x", "number", []),
    dirtyY: module.cwrap("pj_sim_dirty_y", "number", []),
    dirtyWidth: module.cwrap("pj_sim_dirty_width", "number", []),
    dirtyHeight: module.cwrap("pj_sim_dirty_height", "number", []),
    dirtyPartial: module.cwrap("pj_sim_dirty_partial", "number", []),
  };
}

function updateHistory(nextState) {
  if (visited[visited.length - 1] !== nextState) {
    visited.push(nextState);
  }
  history.innerHTML = visited.slice(-20).map((item) => `<li>${item}</li>`).join("");
}

function framebufferBit(bytes, x, y) {
  const index = y * DISPLAY_WIDTH + x;
  return (bytes[index >> 3] >> (index & 7)) & 1;
}

function copyFirmwareFramebuffer(region) {
  const pointer = api.framebuffer();
  const byteCount = api.framebufferBytes();
  const bytes = wasmModule.HEAPU8.subarray(pointer, pointer + byteCount);
  const x0 = Math.max(0, region.x);
  const y0 = Math.max(0, region.y);
  const x1 = Math.min(DISPLAY_WIDTH, x0 + region.width);
  const y1 = Math.min(DISPLAY_HEIGHT, y0 + region.height);

  for (let y = y0; y < y1; y += 1) {
    for (let x = x0; x < x1; x += 1) {
      const offset = (y * DISPLAY_WIDTH + x) * 4;
      const value = framebufferBit(bytes, x, y) ? 0 : 255;
      framebufferImage.data[offset] = value;
      framebufferImage.data[offset + 1] = value;
      framebufferImage.data[offset + 2] = value;
      framebufferImage.data[offset + 3] = 255;
    }
  }
}

function paintFirmwareFramebuffer(action = "render") {
  api.render();
  const state = stateLabel();
  const dirty = dirtyRegion();
  const region = dirty.partial && dirty.width > 0 && dirty.height > 0
    ? dirty
    : { x: 0, y: 0, width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT, partial: 0 };

  copyFirmwareFramebuffer(region);
  ctx.putImageData(framebufferImage, 0, 0, region.x, region.y, region.width, region.height);
  stateName.textContent = state;
  if (viewPicker && Object.hasOwn(REVIEW_ROUTES, state)) {
    viewPicker.value = state;
  }
  powerButton.setAttribute("aria-pressed", state === "static" ? "false" : "true");
  powerButton.title = state === "static" ? "Power on" : "Power off";
  history.dataset.dirtyRegion = JSON.stringify(dirty);
  updateHistory(state);
  traceDebug("firmware.render", { action, state, dirty, painted: region });
}

function dispatchFirmware(action, callback, acknowledgePlaybackStop = false) {
  if (!api) {
    logAction(`${action}: firmware runtime not ready`);
    return;
  }
  const previous = stateLabel();
  const changed = callback();
  const playbackAcknowledged = acknowledgePlaybackStop && api.playbackState() === PLAYBACK_STOPPING;
  if (playbackAcknowledged) {
    api.setAudioState(api.recordState() !== 0 ? 1 : 0, 0);
  }
  paintFirmwareFramebuffer(action);
  const current = stateLabel();
  logAction(`${action}: ${previous} -> ${current}${changed === 0 ? " (no change)" : ""}`);
  traceDebug("firmware.event", {
    action,
    previous,
    current,
    changed,
    playbackAcknowledged,
    dirty: dirtyRegion(),
  });
}

function handlePowerToggle(action = "power") {
  dispatchFirmware(action, () => {
    if (stateLabel() === "static") {
      api.wake();
    } else {
      api.sleep();
    }
    return 1;
  });
}

function handleAuxShort(action = "A aux") {
  dispatchFirmware(action, () => api.auxShort(), true);
}

function handleAuxLong(action = "A aux long") {
  dispatchFirmware(action, () => api.auxLong(), true);
}

function handleAuxDouble(action = "A aux double") {
  dispatchFirmware(action, () => api.auxDouble(), true);
}

const auxClickDetector = createAuxClickDetector({
  onShort: handleAuxShort,
  onDouble: handleAuxDouble,
  now: () => performance.now(),
  setTimer: (callback, delay) => setTimeout(callback, delay),
  clearTimer: (timer) => clearTimeout(timer),
});

const auxPressDetector = createAuxPressDetector({
  onShort: scheduleAuxShort,
  onLong: (label) => {
    cancelPendingAuxShort();
    handleAuxLong(label);
  },
  now: () => performance.now(),
  setTimer: (callback, delay) => setTimeout(callback, delay),
  clearTimer: (timer) => clearTimeout(timer),
});

function cancelPendingAuxShort() {
  auxClickDetector.cancel();
}

function scheduleAuxShort(label) {
  auxClickDetector.click(label);
}

function resetSimulator() {
  cancelPendingAuxShort();
  auxPressDetector.cancel();
  dispatchFirmware("reset", () => {
    api.reset();
    seedDynamicContent();
    return 1;
  });
}

function openReviewView(name) {
  const route = REVIEW_ROUTES[name];
  if (!api || !route) {
    logAction(`view ${name}: unavailable`);
    return;
  }
  cancelPendingAuxShort();
  api.reset();
  seedDynamicContent();
  for (const step of route) {
    if (step === "wake") {
      api.wake();
    } else if (step === "back") {
      api.auxLong();
    } else if (step === "aux") {
      api.auxShort();
    } else {
      api.touchTap(step[0], step[1]);
    }
  }
  paintFirmwareFramebuffer(`review ${name}`);
  viewPicker.value = name;
  logAction(`opened ${stateLabel()} through firmware inputs`);
}

function captureDisplay() {
  const link = document.createElement("a");
  link.download = `pocket-journal-${stateLabel()}.png`;
  link.href = canvas.toDataURL("image/png");
  link.click();
  logAction(`captured ${stateLabel()} display`);
}

function seedDynamicContent() {
  api.setStatus(84, 22, 45);
  api.setTime(9, 41, 2026, 6, 6);
  api.seedReviewNotes();
}

function handleCanvasClick(x, y) {
  dispatchFirmware("display tap", () => api.touchTap(x, y), true);
}

function attachAuxButton(button, label) {
  button.addEventListener("pointerdown", (event) => {
    if (auxPressPointerId !== null) {
      return;
    }
    auxPressPointerId = event.pointerId;
    auxPressDetector.press(label);
    button.setPointerCapture?.(event.pointerId);
    traceDebug("event.aux.pointerdown", {
      source: label,
      pointerId: event.pointerId,
      pointerType: event.pointerType,
    });
  });

  button.addEventListener("pointerup", (event) => {
    if (auxPressPointerId === null || event.pointerId !== auxPressPointerId) {
      return;
    }
    auxPressPointerId = null;
    traceDebug("event.aux.pointerup", { source: label });
    auxPressDetector.release(label);
  });

  button.addEventListener("pointercancel", (event) => {
    traceDebug("event.aux.pointercancel", { source: label, pointerId: event.pointerId });
    auxPressPointerId = null;
    auxPressDetector.cancel();
  });

  button.addEventListener("keydown", (event) => {
    if (!event.repeat && (event.key === "Enter" || event.key === " ")) {
      event.preventDefault();
      traceDebug("event.aux.keydown", { source: label, key: event.key });
      scheduleAuxShort(`${label} key`);
    }
  });
}

canvas.addEventListener("click", (event) => {
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor(((event.clientX - rect.left) / rect.width) * DISPLAY_WIDTH);
  const y = Math.floor(((event.clientY - rect.top) / rect.height) * DISPLAY_HEIGHT);
  traceDebug("event.canvas.click", {
    clientX: event.clientX,
    clientY: event.clientY,
    rect: { left: rect.left, top: rect.top, width: rect.width, height: rect.height },
    x,
    y,
  });
  handleCanvasClick(x, y);
});

window.addEventListener("resize", updateDisplayScale);

resetButton.addEventListener("click", () => {
  traceDebug("event.reset.click");
  resetSimulator();
});

powerButton.addEventListener("click", () => {
  traceDebug("event.power.click", { source: "panel" });
  handlePowerToggle("side power");
});

simPowerButton.addEventListener("click", () => {
  traceDebug("event.power.click", { source: "device" });
  handlePowerToggle("D power");
});

attachAuxButton(bootButton, "side aux");
attachAuxButton(simAuxButton, "A aux");

window.addEventListener("keydown", (event) => {
  if (event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement) {
    return;
  }
  const key = event.key.toLowerCase();
  if (key === "a" && event.shiftKey && !event.repeat) {
    event.preventDefault();
    traceDebug("event.keydown", { key, mappedTo: "aux-long" });
    cancelPendingAuxShort();
    handleAuxLong("Shift+A key");
  } else if (key === "a" && !event.repeat) {
    event.preventDefault();
    traceDebug("event.keydown", { key, mappedTo: "aux" });
    scheduleAuxShort("A key");
  } else if (key === "d" && !event.repeat) {
    event.preventDefault();
    traceDebug("event.keydown", { key, mappedTo: "power" });
    handlePowerToggle("D key");
  } else if (key === "r" && !event.repeat) {
    event.preventDefault();
    traceDebug("event.keydown", { key, mappedTo: "reset" });
    resetSimulator();
  }
});

openViewButton?.addEventListener("click", () => openReviewView(viewPicker.value));
viewPicker?.addEventListener("change", () => openReviewView(viewPicker.value));
captureButton?.addEventListener("click", captureDisplay);

debugDumpButton?.addEventListener("click", () => {
  traceDebug("event.debug_flush.click");
  flushDebugLog();
  logAction("debug log flushed to .logs/");
});

window.pocketJournalSimulator = {
  power: () => handlePowerToggle("debug power"),
  bootShort: () => handleAuxShort("debug aux"),
  bootLong: () => handleAuxLong("debug aux long"),
  bootDouble: () => handleAuxDouble("debug aux double"),
  reset: resetSimulator,
  state: () => stateLabel(),
  action: () => actionLine.textContent,
  audioState: () => api ? { recording: api.recordState(), playback: api.playbackState() } : null,
  setAudioState: (recording, playback) => dispatchFirmware(
    "board audio update",
    () => {
      api.setAudioState(recording ? 1 : 0, playback ? 1 : 0);
      return 1;
    },
  ),
  debugLog: () => debugDump(),
  flushDebugLog,
  clearDebugLog,
};

setInterval(() => {
  if (!api) {
    return;
  }
  const changed = api.tick();
  if (changed) {
    paintFirmwareFramebuffer("tick");
  }
}, 1000);

async function loadFirmwareRuntime() {
  traceDebug("wasm.fetch.start", { url: WASM_MODULE_URL });
  const factory = (await import(WASM_MODULE_URL)).default;
  wasmModule = await factory({
    locateFile(path) {
      return `generated/${path}`;
    },
  });
  api = bindApi(wasmModule);
  api.init();
  seedDynamicContent();
  traceDebug("wasm.loaded", {
    width: api.displayWidth(),
    height: api.displayHeight(),
    framebufferBytes: api.framebufferBytes(),
  });
}

async function boot() {
  updateDisplayScale();
  await loadFirmwareRuntime();
  paintFirmwareFramebuffer("boot");
  logAction("firmware simulator ready");
}

traceDebug("module.ready", {
  elements: {
    canvas: Boolean(canvas),
    ctx: Boolean(ctx),
    stateName: Boolean(stateName),
    actionLine: Boolean(actionLine),
    powerButton: Boolean(powerButton),
    bootButton: Boolean(bootButton),
    simAuxButton: Boolean(simAuxButton),
    simPowerButton: Boolean(simPowerButton),
    debugDumpButton: Boolean(debugDumpButton),
  },
});

boot().catch((error) => {
  traceDebug("boot.failed", { error: safeError(error) });
  logAction("boot failed; check .logs/simulator-debug-latest.json");
  console.error("Simulator boot failed", error);
});
