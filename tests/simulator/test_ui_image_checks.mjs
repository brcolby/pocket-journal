import assert from "node:assert/strict";
import { analyzeFrame, validateFrame } from "../../tools/ui_image_checks.mjs";

const frame = (width = 20, height = 20) => new Uint8Array(width * height);
const set = (pixels, width, x, y) => { pixels[y * width + x] = 1; };

assert.throws(() => analyzeFrame(new Uint8Array(3), 2, 2), /expected 2x2/);

let pixels = frame();
let metrics = analyzeFrame(pixels, 20, 20);
assert.ok(validateFrame(metrics).includes("frame is blank"));

pixels = frame();
for (let y = 5; y < 15; y += 1) for (let x = 5; x < 15; x += 1) set(pixels, 20, x, y);
metrics = analyzeFrame(pixels, 20, 20);
assert.equal(metrics.width, 20); assert.equal(metrics.height, 20);
assert.equal(metrics.blackPixels, 100); assert.equal(metrics.largestComponent.width, 10);
assert.deepEqual(validateFrame(metrics), []);

pixels = frame();
for (let x = 0; x < 20; x += 1) set(pixels, 20, x, 0);
metrics = analyzeFrame(pixels, 20, 20);
assert.ok(validateFrame(metrics).some((error) => error.includes("display edge")));
assert.ok(validateFrame(metrics).some((error) => error.includes("connected component")));
assert.deepEqual(validateFrame(metrics, { maxEdgePixels: Infinity, maxComponentFraction: 1, maxImbalance: 1, minEdgeSides: 1 }), []);
assert.ok(validateFrame(metrics, { maxEdgePixels: Infinity, maxComponentFraction: 1, maxImbalance: 1, minEdgeSides: 4 })
  .some((error) => error.includes("only 3 display edges")));

pixels = frame();
for (let y = 1; y < 19; y += 1) for (let x = 1; x < 19; x += 1) set(pixels, 20, x, y);
assert.ok(validateFrame(analyzeFrame(pixels, 20, 20)).some((error) => error.includes("too high")));

console.log("UI image-analysis tests passed");
