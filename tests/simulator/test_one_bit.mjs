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

assert.equal(fontAsset.family, "Space Mono Bold");
assert.equal(fontAsset.source, "assets/fonts/SpaceMono-Bold.ttf");
assert.ok(Number.isInteger(fontAsset.sizes["2"].line_height));
assert.ok(fontAsset.sizes["2"].glyphs.A.rows.join("").includes("1"));
assert.ok(fontAsset.sizes["2"].glyphs.Z.rows.join("").includes("1"));

console.log("simulator one-bit tests passed");
