# Pocket Journal

Pocket Journal is a derisking workspace for a Waveshare ESP32-S3 Touch e-Paper 1.54 device and a partner laptop app.

The repo is split into four main areas:

- `firmware/`: ESP-IDF firmware skeleton and shared UI/state-machine core.
- `simulator/`: browser-based 200x200 black/white e-paper simulator for UI iteration before hardware arrives.
- `partner/`: CLI-first laptop app for provisioning, sync, transcription, calendar sync, and settings.
- `docs/`: hardware, install, API, sync, and test notes.

## Current Status

This is an initial derisk implementation. The shared UI core is real C code with native tests. The ESP-IDF app and hardware services are structured around the target board, but hardware drivers are intentionally isolated until the actual V1/V2 board revision is verified.

## Quick Start

Run the native UI tests:

```sh
make test-ui
```

Run the partner unit tests:

```sh
make test-partner
```

Open the simulator:

```sh
make simulator
```

Then open `http://127.0.0.1:8765`.

Run the partner CLI from source:

```sh
cd partner
PYTHONPATH=src python -m pocket_journal_partner --help
```

## Hardware Assumptions

- Target SKU: Waveshare `34211`, ESP32-S3-Touch-ePaper-1.54.
- Display: 200x200, black/white e-paper.
- Storage: FAT32 TF card required for audio and local device data.
- Networking: USB-C provisioning by default, optional BLE provisioning, then Wi-Fi station mode.
- Partner host: Apple Silicon macOS and Linux first.
- Google Calendar OAuth credentials stay on the partner host.
