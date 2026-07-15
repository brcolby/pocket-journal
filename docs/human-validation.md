# Human Validation Checklist

Beads is the source of truth. This file is the concise operator path for one
low-frequency, large-batch hardware pass. Human notes are mildly lossy evidence:
record what was actually observed, and agents will map that evidence to the
criteria it necessarily exercised.

Last synchronized with `bd human list`: 2026-07-15 (29 open/in-progress beads).

Do not start this pass until all agent-executable work for the batch is complete,
the full automated/simulator/build gates pass, and one exact build is nominated.
Do not repeat a failed check on an unchanged build.

## Nominated Build

- Ordinary fail-closed firmware source/version: `38a773d`; image SHA-256
  `fe0eeca84a99d7ada5d3e5427f3b2849c3b0b3d565a8a01ae45f3a88240f6966`.
  Rollback is compiled in, but OTA write remains unavailable without configured
  application and manifest verification keys.
- Flashed signed-development factory: version `38a773d`; image SHA-256
  `3717367a8ec8fffb571c42f7893116b9d87f8f46bfbc90a5d86fbbeb190c5391`.
  It uses signed-app verification without hardware Secure Boot plus rollback;
  it changes no eFuse and makes no production-release claim.
- OTA candidate: version `0.1.0`; image SHA-256
  `f7a3ba51fb6156b20ef0973cef3687608404a8d2632d01cb8c93eef05ee913df`;
  manifest SHA-256
  `a425886671aad354d9f291e1e0f248da94065a2a0bd87b5934d8cfdf0a6875bb`;
  signature SHA-256
  `d0a2cc83e0223236d875cff9c8967846d204dc5ffccd7ecf1c1d754a58ffa93f`.
  The first factory update is expected to select `ota_0`; device preflight is
  authoritative. Adjacent image, manifest, and signature files are in
  `/private/tmp/pj-final-ota-candidate/` for this development pass.
- Public-key SHA-256 fingerprints: manifest P-256 DER
  `b8cd13c9dcfa943ef4804039573bee1849cb9e1158e73cebb4eb079c6bb84a2c`;
  application-signing public binary
  `eeaf88a36f8add64d15160fb8c4ad2d5aa5800cb83992f99d4dfba6c915b9e86`.
- Partner source/version: `38a773d`, package `0.1.0`; the editable
  `partner/.venv` has its declared mDNS dependency installed and passes real
  USB plus LAN discovery.
- Flash: 2026-07-15 from `/private/tmp/pj-final-signed-factory` with
  `python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800
  --before default-reset --after hard-reset write-flash @flash_args`.
  Every written region verified; no monitor was opened or left attached.
- Before testing, the operator records the board revision, enclosure state,
  battery/power source, and SD-card identity in the result notes.
- Automated evidence: full `make test` (including 265 partner tests),
  `make test-simulator-runtime ui-gallery`, ordinary and signed ESP-IDF 6.0.1
  builds, `espsecure verify-signature`, partner bundle inspection, OpenSSL
  manifest verification, and image-analysis checks all passed. On the flashed
  target, bounded USB/LAN status, interval reset, Wi-Fi diagnostics, mDNS,
  correct-token authentication, and three negative-token cases passed; the USB
  descriptor was released afterward.

Already accepted on earlier hardware and not repeated here: default USB-C
provisioning, automatic host-time anchoring during provisioning, bounded Wi-Fi
retry progression, durable interval reset, USB autodiscovery, and invalid-WAV
rejection (`pocket-journal-iig`, `pocket-journal-223`, `pocket-journal-r7s`,
`pocket-journal-3i4`, `pocket-journal-b96`, `pocket-journal-pc3`; the last is an
accepted criterion of a still-open broader storage bead). Exact firmware
`38a773d` also obtained DHCP, exposed credential-safe AP diagnostics, and was
discovered through mDNS, closing `pocket-journal-d3d.1`.

## One Ordered Pass

### 1. Boot, Network, And Time

- [ ] Boot the nominated firmware once, then reboot once. Confirm normal boot,
  PSRAM detection, display/touch/RTC/sensor/SD/audio initialization, and no boot
  loop. Preserve a bounded boot log.
- [ ] After the requested reboot, confirm the already configured network remains
  connected and mDNS-discoverable. If it fails, capture SSID-visible, RSSI,
  channel, auth mode, disconnect reason, retry count, and backoff without
  credentials (`pocket-journal-d3d`).
- [ ] Against the discovered LAN API, confirm the saved token works while missing,
  malformed, and wrong bearer values fail consistently. Reboot and confirm the
  token still works; no token or Wi-Fi credential may appear in status or logs
  (`pocket-journal-3ie`).
- [ ] Confirm the displayed local time becomes correct through background SNTP,
  remains correct after reboot, and reports a truthful published/RTC failure
  state. Start a duration timer before sync and confirm it does not jump
  (`pocket-journal-2f2`).
- [ ] With an expendable test SSID and credential, run `pj provision --ble`.
  Confirm credential/token writes require an encrypted paired connection, commit
  obtains DHCP and `_pocket-journal._tcp` mDNS, advertising exits afterward, and
  reboot plus an AP off/on or forced disconnect reconnects from NVS. Status and
  Sync must show truthful failure/recovery without logging credentials or token
  (`pocket-journal-d3d`). This validates the optional transport; it does not
  approve encrypted Just Works as production MITM protection or close
  `pocket-journal-db1`.

### 2. Display, Touch, And AUX

- [ ] Inspect Clock, Home, Notes, playback, Record, Alarm, Stopwatch, Timer,
  Interval, Settings, Volume, and Sync. Confirm full-bleed contiguous geometry,
  uppercase chrome, thick boundaries, large unclipped text, three notes per page,
  direct touch targets, and no focus dot, inverted focus, or hidden focus cycle
  (`pocket-journal-nz5`, `pocket-journal-2ji`). Change Volume, Light/Dark,
  12/24, C/F, transcript size, Alarm, Timer, and Interval presets; reboot and
  confirm each retained value, Alarm AM/PM clarity, and plausible
  temperature/humidity (`pocket-journal-wsl`, `pocket-journal-aw7`).
- [ ] Confirm AUX short performs only an obvious primary action, AUX double starts
  quick Record only from Home/Clock, and AUX hold fires Back once at 500 ms while
  still held. Release must do nothing; holding through a transition must not
  wedge navigation. Confirm double-click is rejected everywhere except Home and
  Clock (`pocket-journal-61u`, `pocket-journal-2ji`, `pocket-journal-d8j`).
- [ ] Exercise touch edges/corners and several minutes of Clock, Stopwatch,
  Timer, Interval, note paging, settings, and volume updates. Confirm dynamic
  time updates are no faster than 1 Hz, direct input stays responsive, pixels
  remain coherent, ghosting stays bounded, and logs show no private DMA
  allocation, display refresh, or BUSY timeout error
  (`pocket-journal-1vc`, `pocket-journal-zi6`, `pocket-journal-e43`,
  `pocket-journal-jjt`). Preserve bounded before/after full, partial, and no-op
  refresh counts, latency, dirty-area, BUSY, and error metrics.

### 3. Record, Listen, And Companion

- [ ] Record at least five seconds. Confirm elapsed time advances from captured
  audio, AUX Back returns immediately with no Saving screen or blocking repaint,
  and exactly one valid note appears after background finalization. Play/pause it,
  then AUX Back from both playing and paused states. Play it again through EOF and
  confirm authoritative idle state. No corrupt note may appear after an
  interrupted capture. Attempt immediate record re-entry and confirm it is
  rejected until finalization completes (`pocket-journal-sm1`,
  `pocket-journal-te0`, `pocket-journal-61u`).
- [ ] Run the nominated partner's consolidated USB sync/settings workflow. Confirm
  USB descriptors are released, the local library has one idempotent audio row,
  playback works, local transcription uploads and is readable on-device, and
  every retained settings field round-trips without restoring removed Calendar,
  custom Home, or runtime Static-art features. Record exact command output.
- [ ] Transcribe a short, intelligible note from this device with the nominated
  fixed CPU-only Q5 model. Confirm the transcript describes the recording,
  remains readable on-device, and silence/no-speech does not become a fabricated
  transcript. Preserve the benchmark report plus the source recording for later
  enclosure/noise comparison (`pocket-journal-zon`).
- [ ] Using an expendable or backed-up SD card, verify removal/reinsert/remount,
  the bounded low/full-space path, and one interrupted write. While unmounted,
  attempt Record and playback and confirm clear UI/API failure, authoritative idle
  state, and no phantom note. Existing valid notes must survive remount and
  capacity/health must stay truthful. Do not repeat corrupt-WAV rejection, which
  is already accepted (`pocket-journal-pc3`, `pocket-journal-te0`).
- [ ] Start Sync on-device while the nominated companion is discoverable. Confirm
  truthful pending/active/complete progress, no duplicate note/transcript, and a
  responsive UI. Then stop the companion, request Sync, and confirm an actionable
  offline/retry state. Restart it and confirm the same durable operation resumes
  exactly once. Logs and captures must not expose the bearer token; a forged,
  stale, altered, cross-device, or cross-generation response must not acknowledge
  work (`pocket-journal-kin`).

### 4. Time Apps And Alerts

- [ ] Start, pause, resume, navigate away from, and return to Stopwatch and Timer.
  Confirm values remain stable and Back pauses/resets/returns in one hold.
- [ ] Start a 90-second Interval at round 0 and observe rounds 1 and 2. Duration
  must not drift; no Stop / Interval / Recovered modal may appear; each round may
  chime once only. Reboot once mid-round and confirm the expected exact recovery
  outcome, stable duration, and no duplicate chime or modal. Reset it before
  leaving the device (`pocket-journal-8q5`, `pocket-journal-xl8`).
- [ ] At an approved nonzero volume, trigger one Alarm or Timer chime. Confirm one
  roughly one-second nonmodal sound, automatic exact-alert clearing, responsive
  navigation, and PA idle afterward. Repeat at volume zero and confirm silence.
  Record any pop, click, crackle, or harshness (`pocket-journal-oi9`,
  `pocket-journal-xl8`).
- [ ] Arm a short Timer, enter sleep, and confirm one RTC/GPIO5 wake and no wake
  loop. While a timer is armed, confirm BOOT/manual wake still works and an early
  manual wake can re-enter sleep (`pocket-journal-54s`).

### 5. Signed OTA And Recovery

- [ ] Begin on the nominated factory image and use a development-only signed OTA
  image made from the documented temporary-key procedure. Confirm the companion
  reports the exact running/target version, digest, slot, preflight result, reboot
  requirement, and final boot outcome. Confirm the device remains usable during
  preflight and refuses recording, playback, sync, or storage mutation only while
  the firmware body is actively being written (`pocket-journal-i4s.2`,
  `pocket-journal-i4s`).
- [ ] Complete one valid factory-to-semver update and reboot. Confirm the first UI
  render and required services succeed before the image becomes confirmed, the
  LAN/USB token still works after provisioning changes, and the same or older
  version cannot be replayed. Do not create or publish a release tag during this
  development validation.
- [ ] Reject a bad manifest signature, changed manifest field, wrong board or
  project, invalid image signature, digest mismatch, oversized body, active-slot
  target, stale response, and concurrent second upload without changing the boot
  partition or releasing the first upload's mutation exclusion.
- [ ] Interrupt one upload and one first boot at controlled points. Confirm the
  upload abort leaves the previous image bootable, a failed/unconfirmed first
  boot rolls back to the exact prior partition, and status reports the truthful
  failure/rollback outcome. Never remove power while flash is actively being
  written unless the operator has explicitly accepted that destructive test.

### 6. Physical Power

- [ ] On USB-only power, press PWR off once: only compiled splash art appears and
  the device sleeps. Press PWR once to wake directly to launcher Home with fresh
  time/sensor data. Confirm the first full refresh and first edge/corner touch
  succeed after wake. AUX/touch must never enter or escape Static; bounce or a
  held button must not double-toggle. If a recoverable display init/refresh error
  is observed under the stress pass, confirm the next full refresh restores a
  coherent panel without a false successful metric (`pocket-journal-ap4.1`,
  `pocket-journal-ap4`, `pocket-journal-jjt`, `pocket-journal-1vc`).
- [ ] Repeat the same off/wake sequence on battery power and record any current,
  LED, disconnect, or wake-source anomaly (`pocket-journal-ap4.1`,
  `pocket-journal-ap4`).

## Separate Human Decisions And Physical Work

These are not reasons to interrupt the consolidated core pass.

- [ ] Choose the secure BLE possession, recovery, and credential-reset UX
  (`pocket-journal-db1`). USB-C remains the default provisioning path.
- [ ] Approve the portal authentication/session and capability model before more
  browser-control work (`pocket-journal-kky`).
- [ ] Correct the enclosure microphone/speaker openings, then capture controlled
  before/after audio before detailed gain, filtering, crackle, or loudness tuning
  (`pocket-journal-cpk`; follow-up tuning remains separate).

## Result Template

For each failed or checked section, record the firmware/partner versions, exact
steps, expected and observed behavior, board/power/enclosure state, and the small
relevant log/photo/recording excerpt. State which bead can close or the narrow
follow-up defect. After reconciliation, agents update Beads and this checklist;
unchanged verified behavior is not retested.
