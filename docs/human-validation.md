# Human Validation Checklist

This is the working hardware and product-decision checklist. Check an item here
after testing it and add a short result beneath it. Beads remains the source of
truth; include the bead ID when reporting a result so an agent can update or
close the correct issue.

The current visual treatment is accepted as the initial-release baseline.
Future visual refinement is non-blocking unless hardware testing finds a
legibility, navigation, clipping, refresh, or other usability defect.

Last synchronized with `bd human list`: 2026-07-14 (15 beads).

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
- Firmware `06a2a4f` required one physical USB replug after the previously
  wedged image, then flashed successfully without a monitor. USB status and
  explicit host-time sync both completed and released the descriptor.
- On that same `06a2a4f` boot, recording wipe exceeded the eight-second host
  timeout. The host descriptor closed, but the next passive application/ROM
  probe was unresponsive even though no host process owned the port
  (`pocket-journal-6ot`, `pocket-journal-rgo`).
- Firmware `f3318d2` flashed successfully without a monitor and reported its
  exact running version over USB. A new bounded USB maintenance command then
  stopped the unattended interval and returned `silenced=true`, `reset=true`,
  `persisted=true`, and `state_changed=true`; status still reported audio and
  storage ready, and no host process or descriptor remained
  (`pocket-journal-ti0`, closed). Follow-up audit found that this response did
  not distinguish interval state from unrelated controller changes and could
  report persistence without an NVS write, so `pocket-journal-3i4` was reopened.
- Exact firmware `558770f` replaced `f3318d2` without a monitor. Its durable
  reset force-writes NVS, drops stale queued interval commands, checks active
  and pending interval state, and waits for audio-worker cleanup. Both the
  initial command and an idempotent retry returned
  `interval_active_after=false`, `persisted=true`, and `silenced=true`. After a
  bounded RTS reboot, status reported exact version `558770f` with audio and
  storage ready, and another reset returned
  `interval_active_before=false` and `interval_active_after=false`.
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
- An early build produced no audible timer or interval alert even though logs
  showed the output path running. A later persisted interval produced clearly
  audible periodic one-shot chimes at nonzero volume. Nonzero audibility is
  established; volume-zero silence, sound quality, and PA idle still need one
  intentional check (`pocket-journal-oi9`).
- AUX Back waited for release, holding AUX through a transition could wedge the
  UI, interval round one changed screen and duration, and dynamic time screens
  used disruptive full refreshes (`pocket-journal-61u`, `pocket-journal-8q5`,
  `pocket-journal-zi6`).
- A later interval failure rendered a large square Stop symbol with
  `INTERVAL` and `RECOVERED` away from the Interval page. Treat this as exact
  evidence for the durable-alert/modal regression in `pocket-journal-8q5`, not
  as a general display corruption report.

## Agent Batch In Progress

Do not retest these on the current firmware. They enter the next consolidated
checklist only after their focused blockers pass automated checks and are
included in the next identified firmware build.

- **USB and storage:** `pocket-journal-6ot` has an asynchronous, correlated,
  exclusive wipe worker. Firmware now gates the dual-core worker until the
  operation-ID response has physically drained. Partner commit `2d40a42` then
  kept one descriptor open through terminal polling; target operation `1`
  succeeded and deleted seven audio plus seven note records. A fresh status
  connection remained responsive without a replug but showed idle state and no
  history, proving serial close/open resets the device. POSIX control-line
  lifecycle work remains in `pocket-journal-rgo` and still blocks `6ot` closure.
- **Connectivity:** `pocket-journal-r7s` adds a 15-second connect-attempt
  watchdog so a missing ESP-IDF terminal event becomes `connect_timeout` with
  increasing retry count and bounded backoff. `pocket-journal-d3d` still needs
  an IP, reconnect, mDNS, and truthful sync-state evidence on the next build.
- **Dynamic time UI:** `pocket-journal-zi6` bounds dirty regions and refresh
  cadence. `pocket-journal-8q5` makes every interval alert nonmodal and clears
  only the exact alert after its one-shot audio settles, so restored and live
  rounds cannot render the Stop / Interval / Recovered takeover.
- **Controls and navigation:** `pocket-journal-61u` fires AUX Back at the
  500 ms threshold independently of display refresh and consumes release.
- **Recording:** `pocket-journal-sm1` derives elapsed time only from committed
  PCM, permits navigation during background finalization, blocks record
  re-entry until idle, and preserves one pending completion event.
- **Alert audio:** `pocket-journal-oi9` produces one approximately one-second
  chime per new alert ID. Target nonzero audibility is now confirmed; only
  volume-zero, sound-quality, and PA-idle judgment remains.
- **Automatic time:** `pocket-journal-223` validates minute-precision host
  local/UTC anchors during USB provisioning and emits a structured retry
  command without reprovisioning or rotating the saved token. Background SNTP
  remains `pocket-journal-2f2` after connectivity works.

Software-only closures in this batch do not need hardware repetition:
`pocket-journal-1dx`, `pocket-journal-cf4`, `pocket-journal-1qk`,
`pocket-journal-3yp`, `pocket-journal-5pt`, `pocket-journal-de3`, and
`pocket-journal-ti0` have their simulator, protocol, target-status,
art-fidelity, or image-analysis acceptance evidence recorded in Beads.

## Next Consolidated Hardware Batch

**Nominated firmware: `558770f`.** It passed the complete native, partner, and
simulator test suite plus an ESP-IDF 6.0.1 build, was flashed with hash
verification and no monitor, survived a bounded reboot with the interval still
inactive, and was left with no USB descriptor owner. Use only this exact build
for the next consolidated pass.

When nominated, perform one ordered pass rather than isolated retests:

- [ ] Leave the device unattended for at least ten minutes before intentionally
  starting an interval. Confirm there is no spontaneous chime and no Stop /
  Interval / Recovered takeover (`pocket-journal-3i4`).
- [ ] Default USB provisioning reports `time_sync.state=synced`,
  `validated=true`, and updates the RTC/display without a separate time command
  (`pocket-journal-223`).
- [ ] Wi-Fi diagnostics obtain an IP or progress within 15 seconds to
  `connect_timeout` with retry count greater than zero; they never remain
  indefinitely at `connecting` and retry count zero (`pocket-journal-r7s`,
  `pocket-journal-d3d`).
- [ ] Recording wipe returns an operation ID, status remains responsive during
  and after it, and a passive USB probe succeeds without reset or replug. The
  same-session wipe already succeeds; do not repeat it until serial close/open
  reset has a focused lifecycle fix (`pocket-journal-6ot`, `pocket-journal-rgo`).
- [ ] AUX hold acts once at 500 ms, permits navigation while recording
  finalizes, and produces no second action on release
  (`pocket-journal-61u`, `pocket-journal-sm1`).
- [ ] Primary screens preserve the accepted full-bleed geometry, uppercase
  chrome, contiguous controls, thick shared boundaries, chevron paging, and
  legible unclipped text on physical e-paper (`pocket-journal-nz5`).
- [ ] Clock, stopwatch, timer, and interval update at no more than 1 Hz without
  disruptive full flashes, gray/displaced pixels, or accumulating ghosting
  (`pocket-journal-zi6`).
- [ ] An intentionally started interval stays inline through rounds 0, 1, and
  2 with stable duration and one chime per round; no Stop / Interval / Recovered
  overlay appears. End it with AUX before leaving the device unattended
  (`pocket-journal-8q5`).
- [ ] A recording of at least five seconds advances from captured audio, AUX
  returns immediately, exactly one playable note appears after background
  finalization, and an interrupted capture exposes no corrupt note
  (`pocket-journal-sm1`).
- [ ] At nonzero volume, one timer or interval expiry produces one bounded
  acceptable chime and returns the PA to idle; volume zero is silent
  (`pocket-journal-oi9`).
- [ ] A short timer wakes from static/sleep once through RTC GPIO5, while BOOT
  still wakes independently and an early BOOT wake can re-enter sleep without
  a loop (`pocket-journal-54s`).

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
