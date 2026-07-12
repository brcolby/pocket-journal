# Simulator

The simulator is a browser app for pre-hardware visual iteration. Its screen framebuffer is rendered by the firmware UI core compiled to WebAssembly, so simulator pixels and state transitions come from `firmware/components/pj_ui/pj_ui.c`.

It renders a 200x200 black/white canvas scaled up for inspection. Touch, power, and AUX events are dispatched into the same C state machine used by firmware.

The display canvas is treated as a hardware framebuffer:

- Canvas backing size is fixed at 200x200.
- The visible scale is an integer multiple of the panel size.
- Every render pass copies the exact 1-bit firmware framebuffer.
- Partial refreshes use the dirty region reported by firmware.
- Browser-side drawing is limited to expanding firmware framebuffer bits into canvas pixels.

The firmware renderer uses generated 1-bit font and Carbon icon headers. The generation steps also write JSON copies for asset inspection and simulator contract tests.

Regenerate font assets after changing the source TTF or logical size map:

```sh
make generate-font-assets
make generate-icon-assets
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

For custom resting art, set `localStorage.pocketJournalStaticArt` to the same `/v1/static-art` JSON shape. If none is present, the simulator renders the default smiley bitmap. The generated raster source for that default is `assets/static/smiley.pbm`.

Run it:

```sh
make simulator
```

Open:

```text
http://127.0.0.1:8765
```

While running through `make simulator`, browser debug events are posted back to the local simulator server and written under `.logs/`:

- `.logs/simulator-debug.jsonl`: append-only event records.
- `.logs/simulator-debug-latest.json`: the most recent payload, useful for quick debugging.

The inline bootstrap logger writes to the same endpoint before `src/main.js` loads, so module load failures and watchdog timeouts are captured even when the simulator controls are not wired.

## Interaction Model

- `static`: tap or short-press BOOT/AUX to `time_temp`.
- `time_temp`: short-press BOOT/AUX or tap to `home`; long-press BOOT/AUX returns to `static`.
- `home`: bubble buttons from firmware; notes is the large bubble, with time and settings as smaller bubbles.
- `notes`: record, listen, read.
- `time`: alarm, stopwatch, timer, interval.
- `settings`: sync, volume, dark/light toggle, off.
- `volume`: volume changes inline by tapping the left or right side of the screen.
- `sync`: tap to advance dummy pending/transferred counters; intended as a partial-refresh region.
- `volume`: tap left to lower, right to raise.
- `record`: entering starts recording; pause/resume/finish update only the control/status region.
- `listen` and `read`: show dummy recordings ordered newest first; tap a note to enter detail.
- AUX double-click jumps from any idle screen, including the resting screen, directly into recording. It is ignored while recording, playback, stopwatch, timer, or interval activity is in progress.
- AUX single-click actions wait 350 ms so a second click can be recognized; hold for 650 ms to trigger a long press.
- AUX long press backs out through the firmware parent state graph.
- AUX short press follows the primary action on each screen: Home opens Notes, Settings opens Volume, Volume raises one step, and active media/time screens start, pause, or stop their current action.
