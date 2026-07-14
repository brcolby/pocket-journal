# Human Validation Checklist

This is the working hardware and product-decision checklist. Check an item here
after testing it and add a short result beneath it. Beads remains the source of
truth; include the bead ID when reporting a result so an agent can update or
close the correct issue.

The current visual treatment is accepted as the initial-release baseline.
Future visual refinement is non-blocking unless hardware testing finds a
legibility, navigation, clipping, refresh, or other usability defect.

Last synchronized with `bd human list`: 2026-07-13 (16 beads).

## Batch Validation Strategy

Human testing is intentionally low frequency and broad. Agents should finish a
coherent implementation batch, run all available unit, integration, simulator,
image, and firmware-build checks, and then nominate one exact firmware commit
for a consolidated hardware pass. Do not repeat a failed hardware check on an
unchanged build.

Human notes are treated as mildly lossy evidence: an observation can validate
behavior that necessarily had to work to reach the reported state, even when
the full original checklist was not followed. Each batch result is mapped back
to Beads; demonstrated criteria close, concrete failures become focused bugs,
and genuinely untested hardware risks remain here for a later batch.

The next prompt for human testing should contain a short ordered path through
all newly ready checks, the exact firmware version, prerequisites, expected
results, and the few logs or artifacts worth preserving. Safety, destructive
hardware choices, and hard blockers are the only reasons to request an
out-of-cycle test.

## Ready To Validate

### Controls and display

- [ ] **E-paper and touch baseline** (`pocket-journal-jjt`)
  - Exercise primary screens, touch targets, rapid navigation, wake, and repeated partial refreshes.
  - Record stale pixels, clipping, missed touches, ghosting, and approximate refresh latency.
- [ ] **Clock, unit, and reading-size persistence** (`pocket-journal-aw7`)
  - Toggle 12/24-hour mode directly from the Settings row; verify the change appears immediately and there is no nested Display screen.
  - Confirm Settings rows are Volume, Light/Dark, and 12/24-hour mode.
  - Verify Celsius/Fahrenheit and transcript font-size preferences still round-trip through the settings API and persist without requiring compact-device navigation rows.
  - In 12-hour mode, confirm the main clock has no AM/PM suffix; verify the Alarm screen still disambiguates AM from PM.
  - Reboot and sleep/wake, then confirm all three preferences persist.
  - Compare displayed temperature and humidity with a nearby reference; report whether the values are plausible and stable. When the humidity sensor is unavailable, the clock must show `--%` and `/v1/status` must return `null`, never a plausible placeholder value.
- [ ] **Home layout persistence** (`pocket-journal-bmz`)
  - Change the layout, reboot and sleep/wake, and confirm order and navigation persist.
- [ ] **Static art persistence** (`pocket-journal-lhl`)
  - PUT/GET art repeatedly, reboot and sleep/wake, and verify exact physical pixels.
  - Check missing/corrupt slot and interrupted-write fallback retains last-known-good art.

### Recording, playback, and audio

- [ ] **Settings and volume persistence** (`pocket-journal-wsl`)
  - Change volume and representative settings, reboot and sleep/wake, then verify persistence and real output changes.

### Time, sleep, and wake

- [ ] **RTC alert wake** (`pocket-journal-54s`)
  - Arm a short timer, enter static/sleep, and confirm GPIO5 RTC wake presents it exactly once.
  - While armed, confirm BOOT still wakes independently; try an early BOOT wake and re-sleep.
  - Capture logs for RTC flags and verify there is no repeated wake loop.

### Connectivity, API, and storage

- [ ] **LAN bearer authentication** (`pocket-journal-3ie`)
  - Confirm valid token access after reboot; missing, malformed, and wrong tokens consistently return 401.
  - Check device logs and responses for leaked token or Wi-Fi credentials.
- [ ] **SD-card recovery** (`pocket-journal-pc3`)
  - With backed-up test media, exercise missing/remounted, low/full, corrupt-file, removal, and controlled power-interruption cases.
  - Verify prior valid notes remain readable and partial/corrupt objects are rejected or recovered safely.

## In Current Agent Batch

Do not retest these on the current firmware. They return to `Ready To Validate`
only after their focused blockers pass automated checks and are included in the
next identified firmware build.

- **Controls and navigation** (`pocket-journal-2ji`, `pocket-journal-d8j`): blocked by `pocket-journal-61u`, `pocket-journal-1dx`, and `pocket-journal-sm1`.
- **Editorial UI and refresh behavior** (`pocket-journal-nz5`, `pocket-journal-e43`): blocked by `pocket-journal-1dx`, `pocket-journal-cf4`, and `pocket-journal-zi6`.
- **Recording and board-state truth** (`pocket-journal-te0`): blocked by `pocket-journal-sm1`.
- **Time workflow and alert audio** (`pocket-journal-xl8`, `pocket-journal-oi9`): interval behavior is blocked by `pocket-journal-8q5`; alert audio remains under software diagnosis.
- **Connectivity and automatic time** (`pocket-journal-d3d`): blocked by `pocket-journal-1qk` and `pocket-journal-223`.

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
- [ ] **Playback volume mapping** (`pocket-journal-qza`)
  - Start after the corrected enclosure and recording pipeline are available; play a known note at several volume levels and confirm a useful progression without an unexpectedly loud jump.
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
