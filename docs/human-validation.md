# Human Validation Checklist

This is the working hardware and product-decision checklist. Check an item here
after testing it and add a short result beneath it. Beads remains the source of
truth; include the bead ID when reporting a result so an agent can update or
close the correct issue.

The current visual treatment is accepted as the initial-release baseline.
Future visual refinement is non-blocking unless hardware testing finds a
legibility, navigation, clipping, refresh, or other usability defect.

Last synchronized with `bd human list`: 2026-07-14 (5 beads).

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

## Verified Hardware Evidence

Do not repeat these checks on an unchanged build.

- Default USB-C provisioning completed on firmware `7e6ce1e-dirty` and returned
  the expected device identity with `provisioned=true` (`pocket-journal-iig`,
  closed). A later Wi-Fi association failure does not reopen provisioning.
- A partner command without `--device` selected the attached USB device
  successfully (`pocket-journal-b96`, closed).
- The app reached normal SPI flash boot, initialized the display, touch, RTC,
  environmental sensor, SD card, and audio codec, and accepted USB commands.
- Saved Wi-Fi credentials loaded and the station repeatedly reached
  authentication and association, but it never obtained an IP address
  (`pocket-journal-d3d`, `pocket-journal-1qk`).
- Explicit partner time sync worked. Automatic time establishment after flash
  and provisioning did not (`pocket-journal-223`, `pocket-journal-2f2`).
- Core display and navigation were usable across enough screens to reveal dark
  focused buttons, excess full refreshes, and gray or displaced pixels around a
  minute update (`pocket-journal-jjt`, `pocket-journal-1dx`,
  `pocket-journal-zi6`).
- Record opened, but elapsed time stayed at zero and no completed note appeared
  (`pocket-journal-sm1`).
- Four invalid or interrupted WAV files were rejected without breaking note
  enumeration (`pocket-journal-pc3`). Corrupt-file rejection is verified; the
  remaining storage failure matrix is not.
- Timer and interval alert output was not audible even though logs showed the
  output path starting and finishing at maximum configured volume
  (`pocket-journal-oi9`).
- AUX Back waited for release, holding AUX through a transition could wedge the
  UI, interval round one changed screen and duration, and dynamic time screens
  used disruptive full refreshes (`pocket-journal-61u`, `pocket-journal-8q5`,
  `pocket-journal-zi6`).

## In Current Agent Batch

Do not retest these on the current firmware. They enter the next consolidated
checklist only after their focused blockers pass automated checks and are
included in the next identified firmware build.

- **Controls and navigation:** `pocket-journal-61u` fires AUX Back at the
  threshold and hardens held-button transitions; `pocket-journal-1dx` replaces
  inverted focus with a local indicator and chevrons; `pocket-journal-cf4`
  corrects Timer geometry and focus timeout.
- **Dynamic time UI:** `pocket-journal-zi6` bounds dirty regions and refresh
  cadence; `pocket-journal-8q5` stabilizes interval rounds and durations.
- **Recording:** `pocket-journal-sm1` connects elapsed time to captured bytes and
  publishes exactly one validated note after asynchronous finalization.
- **Alert audio:** `pocket-journal-oi9` now requires one approximately one-second
  chime per new alert ID, never indefinite repetition, and still needs audible
  codec/PA diagnosis.
- **USB reliability:** `pocket-journal-rgo` is completing ROM-mode detection and
  repeated command/flash lifecycle handling. A monitor continuing to run until
  explicitly quit is expected behavior; unintended reset or port retention is
  the defect.
- **Connectivity and time:** `pocket-journal-1qk` adds firmware Wi-Fi phase and
  disconnect diagnostics; `pocket-journal-223` owns automatic USB host-time
  anchoring; `pocket-journal-d3d` owns successful IP, reconnect, mDNS, and sync
  truth. Background SNTP remains `pocket-journal-2f2` after connectivity works.

## Human Decisions And Physical Work

- [ ] **Secure BLE possession UX** (`pocket-journal-db1`)
  - Choose authenticated numeric comparison on the display with AUX confirmation, or a unique per-device proof-of-possession secret.
  - Define lost-partner recovery, bond/secret replacement, provisioning timeout, and factory clearing behavior.
- [ ] **Portal authentication/session UX** (`pocket-journal-kky`)
  - Approve or revise short-lived memory-only browser sessions, same-origin security, conflict revisions, and the initial capability inventory.
- [ ] **Power-mode intent** (`pocket-journal-ap4`)
  - Decide whether PWR means UI suspend, connected standby, disconnected light sleep, deep sleep, or hardware off after observing current hardware behavior.
- [ ] **Correct enclosure acoustics** (`pocket-journal-cpk`)
  - Uncover and correctly orient the microphone path; verify the speaker opening and eliminate enclosure buzz or obstruction.
  - Capture comparable before/after recordings using identical distance, voice, environment, and firmware settings.

## Blocked Future Batch

These checks stay out of `bd human list` until every named agent prerequisite is
complete. Restore the `human` labels only when one exact firmware build is ready.

- **Controls, display, and recording:** `pocket-journal-2ji`,
  `pocket-journal-d8j`, `pocket-journal-nz5`, `pocket-journal-e43`,
  `pocket-journal-te0`, and `pocket-journal-jjt` wait on the current input,
  refresh, Timer, interval, and recording fixes. The next batch should cover
  physical legibility, touch edges, held AUX, playback and recording Back,
  three-note paging, repeated partial refresh, ghosting, and sleep/wake recovery.
- **Settings, LAN APIs, and storage:** `pocket-journal-wsl`,
  `pocket-journal-aw7`, `pocket-journal-3ie`, `pocket-journal-bmz`,
  `pocket-journal-lhl`, and `pocket-journal-pc3` wait on working
  `pocket-journal-d3d` connectivity. Storage removal, remount, full-card, and
  power-loss tests also wait on `pocket-journal-sm1`.
- **Time and RTC wake:** `pocket-journal-xl8` waits on interval, Back, and alert
  fixes. Include `pocket-journal-54s` in the next batch: arm a short timer, enter
  static or sleep, verify one GPIO5 RTC wake and one presentation, exercise an
  early BOOT wake and re-sleep, and capture RTC flags with no wake loop.
- **Audio tuning:** `pocket-journal-qza` and `pocket-journal-im4` wait on both
  the corrected enclosure (`pocket-journal-cpk`) and working recording pipeline
  (`pocket-journal-sm1`). Then test known speech at several volume and gain
  levels without an unexpected jump, crackle, clipping, or enclosure buzz.
- **Transcription:** `pocket-journal-zon` waits on one valid new recording and
  working LAN connectivity. Do not claim transcription validation from the
  current batch.
- **OTA:** `pocket-journal-i4s.2` waits on software child
  `pocket-journal-i4s.1` and target LAN authentication `pocket-journal-3ie`.
  Then validate signed upload, reboot/reconnect/health confirmation,
  interrupted-upload safety, forced rollback, reset/power interruption, and
  factory-to-first-OTA behavior.

## Reporting Results

For each checked item, record:

- Firmware commit or displayed version.
- Hardware/case/SD-card configuration.
- Exact steps and expected versus observed behavior.
- Relevant serial logs, photos, recordings, measurements, or failure text.
- Whether the bead can close or what follow-up remains.
