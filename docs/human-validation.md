# Human Validation Checklist

This is the working hardware and product-decision checklist. Check an item here
after testing it and add a short result beneath it. Beads remains the source of
truth; include the bead ID when reporting a result so an agent can update or
close the correct issue.

Last synchronized with `bd human list`: 2026-07-12.

## Ready To Validate

### Controls and display

- [ ] **Button navigation and typography** (`pocket-journal-2ji`)
  - AUX long/back triggers reliably at the reduced threshold without accidental activation.
  - Common Home, Settings, Volume, Timer, Stopwatch, and Interval actions are usable with buttons.
  - Button labels no longer dominate; timer and other essential values are legible.
- [ ] **Display SPI correction** (`pocket-journal-0bd`)
  - Navigation and refreshes remain reliable with the panel clock capped at 20 MHz.
  - Note any new corruption, timeout, flicker, or ghosting.
- [ ] **E-paper and touch baseline** (`pocket-journal-jjt`)
  - Exercise primary screens, touch targets, rapid navigation, wake, and repeated partial refreshes.
  - Record stale pixels, clipping, missed touches, ghosting, and approximate refresh latency.
- [ ] **Home layout persistence** (`pocket-journal-bmz`)
  - Change the layout, reboot and sleep/wake, and confirm order and navigation persist.
- [ ] **Static art persistence** (`pocket-journal-lhl`)
  - PUT/GET art repeatedly, reboot and sleep/wake, and verify exact physical pixels.
  - Check missing/corrupt slot and interrupted-write fallback retains last-known-good art.

### Recording, playback, and audio

- [ ] **AUX recording shortcut** (`pocket-journal-d8j`)
  - From supported screens, start/stop a recording with AUX and confirm no duplicate activation.
  - Confirm the UI stays usable while save/post-processing continues asynchronously.
- [ ] **Playback volume mapping** (`pocket-journal-qza`)
  - Play a known note at several volume levels; confirm a useful progression and no unexpectedly loud jump.
- [ ] **Settings and volume persistence** (`pocket-journal-wsl`)
  - Change volume and representative settings, reboot and sleep/wake, then verify persistence and real output changes.
- [ ] **Alert audio** (`pocket-journal-oi9`)
  - At nonzero volumes, distinguish alarm, timer, and interval patterns; volume 0 must remain visual-only.
  - Check clicks/pops/crackle, prompt dismiss/snooze, playback preemption, recording defer/resume, and silence while idle.
- [ ] **UI reflects board truth** (`pocket-journal-te0`)
  - During recording, processing, playback, connectivity changes, and failures, verify displayed state matches actual behavior.

### Time, sleep, and wake

- [ ] **RTC alert wake** (`pocket-journal-54s`)
  - Arm a short timer, enter static/sleep, and confirm GPIO5 RTC wake presents it exactly once.
  - While armed, confirm BOOT still wakes independently; try an early BOOT wake and re-sleep.
  - Capture logs for RTC flags and verify there is no repeated wake loop.
- [ ] **Complete time workflow** (`pocket-journal-xl8`)
  - Verify alarm/timer/interval persistence, alert, dismiss, snooze, and exactly-once expiry through navigation and sleep.
  - Verify stopwatch elapsed/running state through navigation, sleep, and reboot.
- [ ] **Power and sleep behavior** (`pocket-journal-ap4`)
  - Record actual PWR/on/off behavior before changing GPIO assumptions.
  - Verify safe sleep entry, wake to Time, fresh sensor readings, and repeated cycles without broken display/audio/storage/network state.

### Connectivity, API, and storage

- [ ] **Wi-Fi, BLE, mDNS, and reconnect** (`pocket-journal-d3d`)
  - Provision over BLE, reboot, and confirm saved Wi-Fi reconnect and `_pocket-journal._tcp` discovery.
  - Disconnect/reconnect the network and confirm status plus pending/uploaded sync counts remain accurate.
- [ ] **LAN bearer authentication** (`pocket-journal-3ie`)
  - Confirm valid token access after reboot; missing, malformed, and wrong tokens consistently return 401.
  - Check device logs and responses for leaked token or Wi-Fi credentials.
- [ ] **SD-card recovery** (`pocket-journal-pc3`)
  - With backed-up test media, exercise missing/remounted, low/full, corrupt-file, removal, and controlled power-interruption cases.
  - Verify prior valid notes remain readable and partial/corrupt objects are rejected or recovered safely.

## Decisions Needed Before More Implementation

- [ ] **Secure BLE possession UX** (`pocket-journal-db1`)
  - Choose authenticated numeric comparison on the display with AUX confirmation, or a unique per-device proof-of-possession secret.
  - Define lost-partner recovery, bond/secret replacement, provisioning timeout, and factory clearing behavior.
- [ ] **Timezone provisioning UX** (`pocket-journal-2f2`)
  - Choose partner-confirmed host timezone during provisioning/settings, or manual timezone selection.
  - The chosen format must support daylight-saving rules; a fixed UTC offset is insufficient.
- [ ] **Portal authentication/session UX** (`pocket-journal-kky`)
  - Approve or revise short-lived memory-only browser sessions, same-origin security, conflict revisions, and the initial capability inventory.
- [ ] **Power-mode intent** (`pocket-journal-ap4`)
  - Decide whether PWR means UI suspend, connected standby, disconnected light sleep, deep sleep, or hardware off after observing current hardware behavior.

## Blocked Until Prerequisites

- [ ] **Correct enclosure acoustics** (`pocket-journal-cpk`)
  - Uncover and correctly orient the microphone path; verify the speaker opening and eliminate enclosure buzz or obstruction.
  - Capture comparable before/after recordings using identical distance, voice, environment, and firmware settings.
- [ ] **Detailed microphone/speaker tuning** (`pocket-journal-im4`)
  - Start only after the corrected enclosure is installed. Run the documented controlled gain/corpus/listening/transcription evaluation.
- [ ] **Partial-refresh optimization** (`pocket-journal-e43`)
  - First complete the `jjt` physical baseline and sustained ghosting/latency measurements.
- [ ] **OTA activation and rollback** (`pocket-journal-i4s.2`)
  - Start after software bead `pocket-journal-i4s.1` completes.
  - Validate signed upload, reboot/reconnect/health confirmation, interrupted upload safety, forced rollback, reset/power interruption, and factory-to-first-OTA behavior.

## Reporting Results

For each checked item, record:

- Firmware commit or displayed version.
- Hardware/case/SD-card configuration.
- Exact steps and expected versus observed behavior.
- Relevant serial logs, photos, recordings, measurements, or failure text.
- Whether the bead can close or what follow-up remains.
