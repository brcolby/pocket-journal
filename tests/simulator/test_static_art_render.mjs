import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { inflateSync } from "node:zlib";

const DISPLAY_WIDTH = 200;
const DISPLAY_HEIGHT = 200;
const FRAMEBUFFER_BYTES = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8;
const PNG_SIGNATURE = Buffer.from("89504e470d0a1a0a", "hex");

function paethPredictor(left, above, upperLeft) {
  const prediction = left + above - upperLeft;
  const leftDistance = Math.abs(prediction - left);
  const aboveDistance = Math.abs(prediction - above);
  const upperLeftDistance = Math.abs(prediction - upperLeft);
  if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) return left;
  if (aboveDistance <= upperLeftDistance) return above;
  return upperLeft;
}

function decodeGrayscalePng(source) {
  assert.deepEqual(source.subarray(0, PNG_SIGNATURE.length), PNG_SIGNATURE, "invalid PNG signature");

  let offset = PNG_SIGNATURE.length;
  let width;
  let height;
  const idatChunks = [];
  while (offset < source.length) {
    const length = source.readUInt32BE(offset);
    const type = source.toString("ascii", offset + 4, offset + 8);
    const dataStart = offset + 8;
    const dataEnd = dataStart + length;
    assert.ok(dataEnd + 4 <= source.length, `truncated ${type} chunk`);

    if (type === "IHDR") {
      assert.equal(length, 13);
      width = source.readUInt32BE(dataStart);
      height = source.readUInt32BE(dataStart + 4);
      assert.equal(source[dataStart + 8], 8, "source must use 8-bit samples");
      assert.equal(source[dataStart + 9], 0, "source must be grayscale without alpha");
      assert.equal(source[dataStart + 10], 0, "unsupported PNG compression method");
      assert.equal(source[dataStart + 11], 0, "unsupported PNG filter method");
      assert.equal(source[dataStart + 12], 0, "source must not be interlaced");
    } else if (type === "IDAT") {
      idatChunks.push(source.subarray(dataStart, dataEnd));
    } else if (type === "IEND") {
      break;
    }

    offset = dataEnd + 4;
  }

  assert.equal(width, DISPLAY_WIDTH);
  assert.equal(height, DISPLAY_HEIGHT);
  assert.ok(idatChunks.length > 0, "PNG has no image data");

  const filtered = inflateSync(Buffer.concat(idatChunks));
  const stride = width;
  assert.equal(filtered.length, height * (stride + 1));
  const pixels = new Uint8Array(width * height);

  for (let y = 0; y < height; y += 1) {
    const rowStart = y * (stride + 1);
    const filter = filtered[rowStart];
    assert.ok(filter <= 4, `unsupported PNG filter ${filter}`);
    for (let x = 0; x < width; x += 1) {
      const index = y * width + x;
      const encoded = filtered[rowStart + x + 1];
      const left = x > 0 ? pixels[index - 1] : 0;
      const above = y > 0 ? pixels[index - width] : 0;
      const upperLeft = x > 0 && y > 0 ? pixels[index - width - 1] : 0;
      let predictor = 0;
      if (filter === 1) predictor = left;
      if (filter === 2) predictor = above;
      if (filter === 3) predictor = Math.floor((left + above) / 2);
      if (filter === 4) predictor = paethPredictor(left, above, upperLeft);
      pixels[index] = (encoded + predictor) & 0xff;
    }
  }

  return pixels;
}

function packPixels(pixels) {
  const packed = new Uint8Array(FRAMEBUFFER_BYTES);
  let blackPixels = 0;
  pixels.forEach((pixel, index) => {
    assert.ok(pixel === 0 || pixel === 255, `source pixel ${index} is not monochrome: ${pixel}`);
    if (pixel === 0) {
      packed[index >> 3] |= 1 << (index & 7);
      blackPixels += 1;
    }
  });
  return { packed, blackPixels };
}

const source = await readFile("assets/static/pocket-journal-default.png");
const { packed: expected, blackPixels } = packPixels(decodeGrayscalePng(source));
const wasmBytes = await readFile("simulator/generated/pj_ui_wasm.wasm");
const { instance } = await WebAssembly.instantiate(wasmBytes, {
  env: { emscripten_resize_heap: () => 0 },
});
const api = instance.exports;

api.__wasm_call_ctors();
api.pj_sim_init();
api.pj_sim_render();
assert.equal(api.pj_sim_display_width(), DISPLAY_WIDTH);
assert.equal(api.pj_sim_display_height(), DISPLAY_HEIGHT);
assert.equal(api.pj_sim_framebuffer_bytes(), FRAMEBUFFER_BYTES);

const start = api.pj_sim_framebuffer();
const actual = new Uint8Array(api.memory.buffer.slice(start, start + FRAMEBUFFER_BYTES));
assert.deepEqual(actual, expected, "default simulator frame differs from the canonical PNG");

console.log(
  `static art simulator fidelity passed: ${DISPLAY_WIDTH * DISPLAY_HEIGHT} pixels, ${blackPixels} black`,
);
