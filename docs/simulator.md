# Simulator

The simulator is a browser app for pre-hardware visual iteration. Its screen framebuffer is rendered by the firmware UI core compiled to WebAssembly, so simulator pixels and state transitions come from `firmware/components/pj_ui/pj_ui.c`.

It renders a 200x200 black/white canvas scaled up for inspection. Touch, power, and AUX events are dispatched into the same C state machine used by firmware.

The display canvas is treated as a hardware framebuffer:

- Canvas backing size is fixed at 200x200.
- The visible scale is an integer multiple of the panel size.
- Every render pass copies the exact 1-bit firmware framebuffer.
- Partial refreshes use the dirty region reported by firmware.
- Browser-side drawing is limited to expanding firmware framebuffer bits into canvas pixels.

The firmware renderer uses generated case-preserving Carbon letters and numbers,
typed Carbon icons, and IBM Plex Mono Bold only for punctuation, spaces, and
unsupported characters. The manifest-driven generator writes byte-identical
firmware headers and simulator JSON.

Regenerate or verify the pinned asset set after changing a source, extraction
rule, or authorized size:

```sh
python3 -m pip install --target /tmp/pj-carbon-python \
  --require-hashes -r tools/carbon-assets-requirements.lock
PYTHONPATH=/tmp/pj-carbon-python make test-assets
```

Build the firmware-backed simulator runtime:

```sh
make generate-simulator-wasm
```

This requires Emscripten's `emcc`. If `emcc` is missing and no generated WASM exists, `make simulator` fails with an install/activation message instead of serving a stale JS-only simulator.

Run the simulator framebuffer contract test:

```sh
make test-simulator
```

Rebuild and test the generated WebAssembly runtime boundary:

```sh
make test-simulator-runtime
```

The simulator starts on the compiled product artwork documented in
[`static-art.md`](static-art.md), using the same packed bytes as firmware. The
WebAssembly runtime test compares all 40,000 rendered pixels with the generated
PBM to catch rotation, mirroring, inversion, or packing changes.

Run it:

```sh
make simulator
```

Open:

```text
http://127.0.0.1:8765
```

The review panel can open each major screen through its normal firmware input
sequence. This is intended for rapid comparison and screenshot capture; it does
not bypass the firmware state machine. The PNG control saves the exact 200x200
display canvas without the simulator shell.

Keyboard controls mirror the two hardware buttons:

- `A`: AUX short press. Press twice within 350 ms for AUX double press.
- `Shift+A`: AUX long press.
- `D`: power or wake.
- `R`: reset the simulator.

While running through `make simulator`, browser debug events are posted back to the local simulator server and written under `.logs/`:

- `.logs/simulator-debug.jsonl`: append-only event records.
- `.logs/simulator-debug-latest.json`: the most recent payload, useful for quick debugging.

The inline bootstrap logger writes to the same endpoint before `src/main.js` loads, so module load failures and watchdog timeouts are captured even when the simulator controls are not wired.

## Interaction Model

- `static`: compiled splash art; the power control wakes directly to `home`.
- `time_temp`: the minute-resolution Clock; tap or AUX short returns to `home`.
- `home`: fixed DXF sectors for Time, Notes, and Settings. AUX long reaches the
  Clock parent.
- `notes`: record, listen, read.
- `time`: alarm, stopwatch, timer, interval.
- `timer` and `interval`: both start from a 60-second fresh-device preset;
  Interval adjusts in 30-second steps down to 30 seconds. Stopwatch
  Play/Pause remains an exact same-layout partial.
- `settings`: four DXF sectors for Volume, Light/Dark (the invariant
  `AsleepFilled` icon), compact 12h/24h, and Sync.
- `volume`: a 0–10 numeric counter uses the upper half; the lower minus and
  plus controls adjust it through exact partial updates.
- `sync`: three centered status/count/detail lines share one maximized font
  size.
- `record`: entry first presents a full `00:00` frame; capture then starts and
  elapsed playable PCM duration is reserved on a hard one-second cadence. AUX
  stops/saves and returns home while processing continues asynchronously.
- `listen` and `read`: show three dummy recordings per page ordered newest first; tap a note to enter detail or use the bottom arrows to move one full page.
- Listen detail uses exact 96 px Play/Pause assets to limit e-paper ghosting;
  the square Stop icon is never compiled or rendered.
- AUX double-click jumps directly into Record only from Home or Clock. It is
  ignored elsewhere and during active media/time work.
- AUX single-click actions wait 350 ms so a second click can be recognized. A long press fires once at the 500 ms threshold while AUX remains held; release is consumed and does not emit another action.
- The AUX long press is the sole Back action and backs out through the firmware parent state graph; child screens have no rendered or touch Back target.
- AUX short invokes only the obvious primary action on a screen. There is no
  hidden focus model; secondary controls are direct touch targets. Active
  media and time screens start or pause without rendering a square Stop icon.
