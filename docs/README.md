# Pocket Journal Documentation

New users should follow these in order:

1. [Install and Flashing](install.md) — release downloads, partner installation,
   serial-port discovery, factory flashing, source builds, and first provisioning.
2. [Partner CLI Reference](cli-reference.md) — every `pj` command, option,
   default, and common example.
3. [Partner Sync](partner-sync.md) — transcription, USB/LAN sync behavior, local
   storage, and library interfaces.

## Hardware and firmware

- [Hardware Reference](hardware-reference.md) — supported V2 board, pin map,
  buses, peripherals, and primary source links.
- [Hardware Bring-Up](hardware-bringup.md) — subsystem bring-up sequence and
  device checks.
- [Storage Recovery](storage-recovery.md) — TF-card health, recovery, and
  failure behavior.
- [Time Alert Model](time-alert-model.md) — alarm, timer, interval, and wake
  semantics.
- [Static Art](static-art.md) — compiled artwork source, generation, and
  fidelity checks.

## Partner and protocols

- [Device API](device-api.md) — authenticated HTTP and bounded USB-C protocol.
- [Device-Initiated Sync](device-initiated-sync.md) — companion discovery,
  mutual authentication, replay handling, and retry behavior.
- [Capability Matrix](capability-matrix.md) — firmware capability names and
  partner requirements.
- [Transcription Benchmark](transcription-benchmark.md) — pinned whisper.cpp
  artifacts, corpus manifest, evidence, and reproduction.

## UI and development

- [Simulator](simulator.md) — local browser simulator and WASM generation.
- [Testing](testing.md) — repository test targets and prerequisites.
- [UI Visual Verification](ui-visual-verification.md) — deterministic image
  capture and review.
- [Home Layout](home-layout.md) — layout data model and geometry generation.

## Product validation

- [Human Validation](human-validation.md) — consolidated hardware/operator
  checks that cannot be closed by automated evidence alone.
