# Testing

## Native UI Tests

```sh
make test-ui
```

These compile the same direct 1-bit compositor and presenter used by firmware
and WASM, verify generated DXF geometry, and exercise state transitions plus
exact framebuffer deltas.

## Generated UI Assets and Geometry

Install the pinned, hashed asset toolchain once, then run the deterministic
source/firmware/simulator parity checks:

```sh
python3 -m pip install --target /tmp/pj-carbon-python \
  --require-hashes -r tools/carbon-assets-requirements.lock
PYTHONPATH=/tmp/pj-carbon-python make test-assets
make test-dxf-geometry
```

The Carbon check regenerates its outputs twice in memory and compares them to
the checked-in headers, simulator JSON, and gallery. The DXF check exhaustively
verifies all 40,000 pixel hit assignments for every generated layout.

## Firmware-backed Simulator

```sh
make test-simulator
make test-simulator-runtime
make ui-gallery
```

The runtime build compiles the production compositor, presenter, and geometry
to WASM; the gallery covers light/dark themes, battery thresholds, note and
playback variants, timer states, Volume extrema, Sync phases, punctuation, and
long transcript text.

## Partner Tests

```sh
make test-partner
```

These run Python unit tests without requiring BLE, Google, Hugging Face, or hardware.

## Hardware Acceptance

After hardware arrives:

1. Add board-specific pin tests.
2. Add TF card read/write smoke tests.
3. Add WAV recording tests.
4. Add LAN API contract tests against the real device.
5. Add end-to-end sync with one sample recording.
