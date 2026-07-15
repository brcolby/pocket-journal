# Pocket Journal

Pocket Journal is a derisking workspace for a Waveshare ESP32-S3 Touch e-Paper 1.54 device and a partner laptop app.

The repo is split into four main areas:

- `firmware/`: ESP-IDF target firmware, board services, and shared UI/state-machine core.
- `simulator/`: browser-based 200x200 black/white e-paper simulator for UI iteration before hardware arrives.
- `partner/`: CLI-first laptop app for provisioning, sync, transcription, diagnostics, and settings.
- `docs/`: hardware, install, API, sync, and test notes.

## Current Status

The ESP-IDF application runs on the Waveshare V2 target with integrated e-paper,
touch, RTC, environmental sensor, SD, ES8311 audio, USB-C partner commands,
Wi-Fi, BLE provisioning, and authenticated LAN services. Native, simulator, and
partner tests cover the shared behavior; consolidated physical validation remains
tracked explicitly for display, input, recording, networking, power, and audio
quality.

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
