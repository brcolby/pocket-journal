import assert from "node:assert/strict";

import {
  AUX_DOUBLE_CLICK_MS,
  AUX_LONG_PRESS_MS,
  createAuxClickDetector,
} from "../../simulator/src/aux_input.js";

assert.equal(AUX_LONG_PRESS_MS, 500);
assert.equal(AUX_DOUBLE_CLICK_MS, 350);

let clock = 0;
let nextTimer = 1;
const timers = new Map();
const events = [];

function setTimer(callback, delay) {
  const id = nextTimer++;
  timers.set(id, { callback, dueAt: clock + delay });
  return id;
}

function clearTimer(id) {
  timers.delete(id);
}

function advanceTo(nextClock) {
  clock = nextClock;
  for (const [id, timer] of [...timers]) {
    if (timer.dueAt <= clock) {
      timers.delete(id);
      timer.callback();
    }
  }
}

const detector = createAuxClickDetector({
  onShort: (label) => events.push(["short", label]),
  onDouble: (label) => events.push(["double", label]),
  now: () => clock,
  setTimer,
  clearTimer,
});

detector.click("single");
advanceTo(AUX_DOUBLE_CLICK_MS);
assert.deepEqual(events, []);
advanceTo(AUX_DOUBLE_CLICK_MS + 1);
assert.deepEqual(events, [["short", "single"]]);

clock = 1000;
detector.click("first");
clock += 200;
detector.click("second");
assert.deepEqual(events.at(-1), ["double", "second double"]);
advanceTo(2000);
assert.equal(events.filter(([kind]) => kind === "double").length, 1);

clock = 3000;
detector.click("cancelled");
detector.cancel();
advanceTo(4000);
assert.equal(events.some(([, label]) => label === "cancelled"), false);

// A delayed event loop must preserve two late single clicks instead of folding them.
clock = 5000;
detector.click("late first");
clock += AUX_DOUBLE_CLICK_MS + 1;
detector.click("late second");
assert.deepEqual(events.at(-1), ["short", "late first"]);
advanceTo(clock + AUX_DOUBLE_CLICK_MS + 1);
assert.deepEqual(events.at(-1), ["short", "late second"]);

console.log("simulator aux input tests passed");
