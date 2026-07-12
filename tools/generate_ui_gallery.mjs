import { deflateSync } from "node:zlib";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { analyzeFrame, validateFrame } from "./ui_image_checks.mjs";

const outputDir = process.argv[2] ?? "build/ui-gallery";
const wasmBytes = await readFile("simulator/generated/pj_ui_wasm.wasm");
const { instance } = await WebAssembly.instantiate(wasmBytes, { env: { emscripten_resize_heap: () => 0 } });
const api = instance.exports;
api.__wasm_call_ctors();
const decoder = new TextDecoder();
function stateName() {
  const start = api.pj_sim_state_name();
  const memory = new Uint8Array(api.memory.buffer);
  let end = start;
  while (memory[end]) end += 1;
  return decoder.decode(memory.subarray(start, end));
}

const crcTable = new Uint32Array(256);
for (let n = 0; n < 256; n += 1) {
  let c = n;
  for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  crcTable[n] = c >>> 0;
}
function crc32(data) {
  let c = 0xffffffff;
  for (const value of data) c = crcTable[(c ^ value) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const name = Buffer.from(type);
  const result = Buffer.alloc(12 + data.length);
  result.writeUInt32BE(data.length, 0); name.copy(result, 4); data.copy(result, 8);
  result.writeUInt32BE(crc32(Buffer.concat([name, data])), 8 + data.length);
  return result;
}
function png(pixels, width, height) {
  const raw = Buffer.alloc((width + 1) * height);
  for (let y = 0; y < height; y += 1) {
    raw[y * (width + 1)] = 0;
    for (let x = 0; x < width; x += 1) raw[y * (width + 1) + x + 1] = pixels[y * width + x] ? 0 : 255;
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4);
  header.set([8, 0, 0, 0, 0], 8);
  return Buffer.concat([Buffer.from("89504e470d0a1a0a", "hex"), chunk("IHDR", header), chunk("IDAT", deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}
function resetToHome() { api.pj_sim_reset(); api.pj_sim_wake(); api.pj_sim_aux_short(); }
function frame() {
  api.pj_sim_render();
  const width = api.pj_sim_display_width(); const height = api.pj_sim_display_height();
  const packed = new Uint8Array(api.memory.buffer, api.pj_sim_framebuffer(), api.pj_sim_framebuffer_bytes());
  const pixels = new Uint8Array(width * height);
  for (let i = 0; i < pixels.length; i += 1) pixels[i] = (packed[i >> 3] >> (i & 7)) & 1;
  return { width, height, pixels };
}

const scenarios = [
  ["static", "static", () => api.pj_sim_reset()],
  ["time-temp", "time_temp", () => { api.pj_sim_reset(); api.pj_sim_wake(); }],
  ["home", "home", resetToHome],
  ["notes", "notes", () => { resetToHome(); api.pj_sim_touch_tap(20, 50); }],
  ["record-active", "record", () => { api.pj_sim_reset(); api.pj_sim_aux_double(); }],
  ["listen-empty", "listen", () => { resetToHome(); api.pj_sim_touch_tap(20, 50); api.pj_sim_touch_tap(40, 150); }],
  ["listen-list", "listen", () => { resetToHome(); api.pj_sim_seed_review_notes(); api.pj_sim_touch_tap(20, 50); api.pj_sim_touch_tap(40, 150); }],
  ["read-list", "read", () => { resetToHome(); api.pj_sim_seed_review_notes(); api.pj_sim_touch_tap(20, 50); api.pj_sim_touch_tap(150, 150); }],
  ["note-detail", "note_detail", () => { resetToHome(); api.pj_sim_set_note_count(1); api.pj_sim_touch_tap(20, 50); api.pj_sim_touch_tap(40, 150); api.pj_sim_touch_tap(20, 50); }],
  ["transcript-detail", "note_detail", () => { resetToHome(); api.pj_sim_seed_review_notes(); api.pj_sim_touch_tap(20, 50); api.pj_sim_touch_tap(150, 150); api.pj_sim_touch_tap(20, 50); }],
  ["time", "time", () => { resetToHome(); api.pj_sim_touch_tap(20, 125); }],
  ["alarm", "alarm", () => { resetToHome(); api.pj_sim_touch_tap(20, 125); api.pj_sim_touch_tap(40, 70); }],
  ["stopwatch", "stopwatch", () => { resetToHome(); api.pj_sim_touch_tap(20, 125); api.pj_sim_touch_tap(150, 70); }],
  ["timer", "timer", () => { resetToHome(); api.pj_sim_touch_tap(20, 125); api.pj_sim_touch_tap(40, 150); }],
  ["interval", "interval", () => { resetToHome(); api.pj_sim_touch_tap(20, 125); api.pj_sim_touch_tap(150, 150); }],
  ["settings", "settings", () => { resetToHome(); api.pj_sim_touch_tap(20, 170); }],
  ["sync", "sync", () => { resetToHome(); api.pj_sim_touch_tap(20, 170); api.pj_sim_touch_tap(40, 70); }],
  ["volume", "volume", () => { resetToHome(); api.pj_sim_touch_tap(20, 170); api.pj_sim_touch_tap(150, 70); }],
  ["alert-alarm", "home", () => { resetToHome(); api.pj_sim_set_alert(1); }],
];

await mkdir(outputDir, { recursive: true });
const rendered = [];
const failures = [];
for (const [name, expectedState, setup] of scenarios) {
  setup();
  if (stateName() !== expectedState) failures.push(`${name}: expected state ${expectedState}, got ${stateName()}`);
  const image = frame();
  if (image.width !== 200 || image.height !== 200) failures.push(`${name}: dimensions ${image.width}x${image.height}`);
  const metrics = analyzeFrame(image.pixels, image.width, image.height);
  for (const error of validateFrame(metrics)) failures.push(`${name}: ${error}`);
  await writeFile(`${outputDir}/${name}.png`, png(image.pixels, image.width, image.height));
  rendered.push({ name, ...image, metrics });
}

const columns = 4; const gap = 8; const cell = 200;
const rows = Math.ceil(rendered.length / columns);
const sheetWidth = columns * cell + (columns - 1) * gap;
const sheetHeight = rows * cell + (rows - 1) * gap;
const sheet = new Uint8Array(sheetWidth * sheetHeight);
for (let i = 0; i < rendered.length; i += 1) {
  const ox = (i % columns) * (cell + gap); const oy = Math.floor(i / columns) * (cell + gap);
  for (let y = 0; y < cell; y += 1) sheet.set(rendered[i].pixels.subarray(y * cell, (y + 1) * cell), (oy + y) * sheetWidth + ox);
}
await writeFile(`${outputDir}/gallery.png`, png(sheet, sheetWidth, sheetHeight));
await writeFile(`${outputDir}/manifest.json`, `${JSON.stringify({ width: 200, height: 200, scenarios: rendered.map(({ name, metrics }) => ({ name, metrics })) }, null, 2)}\n`);
if (failures.length) throw new Error(`UI image checks failed:\n${failures.join("\n")}`);
console.log(`generated ${rendered.length} UI frames and gallery at ${outputDir}`);
