# Human Validation Checklist

This is the working hardware and product-decision checklist. Check an item here
after testing it and add a short result beneath it. Beads remains the source of
truth; include the bead ID when reporting a result so an agent can update or
close the correct issue.

Last synchronized with `bd human list`: 2026-07-12.

## Ready To Validate

### Controls and display

- [ ] **Button navigation and typography** (`pocket-journal-2ji`)
  - AUX long-hold is the sole Back action and triggers reliably at 500 ms without accidental activation.
  - Confirm there is no rendered or touch Back control on child screens.
  - Common Home, Settings, Volume, Timer, Stopwatch, and Interval actions are usable with buttons.
  - Confirm time values are substantially larger, sub-information remains readable, alarm adjustments use clear HR/MIN controls, and control labels are bold without dominating.
  - Confirm interval rounds begin at zero and advance from that zero-indexed value.
- [ ] **Display SPI correction** (`pocket-journal-0bd`)
  - Navigation and refreshes remain reliable with the panel clock capped at 20 MHz.
  - Note any new corruption, timeout, flicker, or ghosting.
- [ ] **E-paper and touch baseline** (`pocket-journal-jjt`)
  - Exercise primary screens, touch targets, rapid navigation, wake, and repeated partial refreshes.
  - Record stale pixels, clipping, missed touches, ghosting, and approximate refresh latency.
- [ ] **Editorial UI redesign** (`pocket-journal-nz5`)
  - Confirm Static shows only the placeholder notebook splash art, with no clock or status text.
  - Confirm Time/Temp uses the full display for the enlarged clock and equal-scale date, temperature/humidity, and battery percentage without clipping or stale pixels.
  - Confirm Home is exactly three contiguous full-width icon buttons; all other button groups are contiguous, reach the screen edges, and have no gutters.
  - Confirm child screens have no rendered or touch Back control; a 500 ms AUX long-hold is the sole Back action.
  - Inspect the icon-only Notes and Time menus, text-led Settings rows, full-screen playback, timer controls, alarm, and volume controls for physical legibility and unambiguous meaning.
  - On Home, short-press cycles destinations and double-press activates the focused secondary destination; focus-zero double-press remains quick record.
  - In Notes, Time, and Settings, double-press cycles visible focus, short-press activates, and long-press returns to the immediate parent.
  - Verify note names and transcript text render uppercase with a correct baseline, no downward-shifted capital letters, word wrapping, and clean ellipses.
  - During active playback, AUX long-hold must stop audio safely before returning to the same selected note and page in Listen; when playback is paused, AUX long-hold must return without restarting audio. During recording, AUX long-hold must save and return immediately while processing continues asynchronously.
  - Verify the record elapsed value, substantially enlarged time values and sub-information, bold controls, clear HR/MIN alarm adjustments, 2x2 timer/interval controls, 90-second interval default, zero-indexed interval round, and filled volume bar.
  - Start and stop timer, stopwatch, and interval activity while watching for stale START/PAUSE icons.
  - Confirm Settings presents direct rows for Volume, Light/Dark, and 12/24-hour mode, with no nested Display screen.
- [ ] **Clock, unit, and reading-size persistence** (`pocket-journal-aw7`)
  - Toggle 12/24-hour mode directly from the Settings row; verify the change appears immediately and there is no nested Display screen.
  - Confirm Settings rows are Volume, Light/Dark, and 12/24-hour mode.
  - Verify Celsius/Fahrenheit and transcript font-size preferences still round-trip through the settings API and persist without requiring compact-device navigation rows.
  - In 12-hour mode, verify both the main clock and alarm clearly distinguish AM from PM.
  - Reboot and sleep/wake, then confirm all three preferences persist.
  - Compare displayed temperature and humidity with a nearby reference; report whether the values are plausible and stable. When the humidity sensor is unavailable, the clock must show `--%` and `/v1/status` must return `null`, never a plausible placeholder value.
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
