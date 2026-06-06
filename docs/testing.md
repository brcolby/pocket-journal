# Testing

## Native UI Tests

```sh
make test-ui
```

These compile the shared C UI core without ESP-IDF and verify state transitions plus 1-bit framebuffer output.

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

