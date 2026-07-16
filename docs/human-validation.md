# Human Validation Checklist

Beads is the source of truth. This file is the operator path for one
low-frequency, large-batch hardware pass. Human notes may be mildly lossy:
record what you actually observed, including blockers, and agents will map that
evidence to the exact Bead acceptance criteria.

Last synchronized with `bd human list`: 2026-07-16 (33 human-validation Beads).
Do not repeat a failed check on unchanged firmware.

## Nominated Build

- Commit and firmware version: `4fe641c`.
- Image: `/private/tmp/pj-final-4fe641c/pocket_journal.bin`.
- Size: 1,642,496 bytes.
- SHA-256:
  `07d0d69ddf917a0438c0b006aeeff39abfc3d345b86d65751a1d894383105dbd`.
- ESP image validation hash:
  `a772e867b562872158e19295d5d3b90f2e08c415ad74b3cb39270e8217f6a276`.
- Built with ESP-IDF 6.0.1 and 22% free in each 2 MiB application partition.
- Flashed successfully as a development image without starting a monitor.
- Live evidence after flashing: exact firmware, SD/audio/Wi-Fi/DHCP ready at
  `192.168.213.241`, storage healthy, fixed UTC offset synchronized, and boot ID
  `764293020`. Ten independent USB status open/request/close cycles kept that
  boot ID, returned without panic, and released the port every time. A bounded
  no-reset check returned complete status with no low-stack warning.
- Existing recordings were removed; the final device inventory is empty. The
  first cleanup removed 4 WAVs, 4 note files, and 2 transcripts but reported a
  retryable residue; after terminal-state inspection, a zero-item cleanup retry
  succeeded.
- The managed validation companion is running on port `8765`; generation 7 is
  acknowledged, online, and succeeded with zero pending files.
- Automated evidence: full `make test` passed with 327 partner tests and all
  native/UI/simulator gates. `make test-simulator-runtime ui-gallery` passed;
  all 25 gallery frames and the contact sheet were image-inspected.
- This is development validation, not a release. No tag or release has been
  created.

Before starting, record board revision, enclosure state, SD identity, power
source, and approximate room conditions. Keep a monitor closed unless an agent
requests bounded diagnostics. Before the Sync checks, ask the agent to confirm
that the managed validation companion is still active and that no unrelated
process owns the USB serial port.

## One Ordered Core Pass

### 1. Boot, Network, And Time

- [ ] Boot once and reboot once. Confirm normal startup, no boot loop, SD/audio
  readiness, automatic connection to the configured network, and truthful
  network state after any disconnect/reconnect (`pocket-journal-d3d`).
- [ ] Confirm local time is correct in 12-hour and 24-hour modes, with no AM/PM
  on the Clock screen. Start a Stopwatch or Timer across one network/SNTP
  refresh and confirm its duration does not jump. Reboot and confirm local time
  and preferences persist (`pocket-journal-2f2`, `pocket-journal-aw7`,
  `pocket-journal-wsl`).

### 2. Display, Touch, And AUX

- [ ] Inspect Clock, Home, note lists, note playback, Record, Alarm, Stopwatch,
  Timer, Interval, Settings, Volume, and Sync. Confirm uppercase text,
  contiguous edge-to-edge controls, thick boundaries, large unclipped type, no
  top title or back button, full-screen playback/recording controls, and a
  full-height Volume bar (`pocket-journal-nz5`, `pocket-journal-2ji`).
- [ ] Exercise digit boundaries `09 -> 10`, `19 -> 20`, `39 -> 40`, and
  `59 -> 60` across Stopwatch, Timer, Interval, and Record. Confirm digits
  advance in order, `1` and `9` are upright and distinct, play/pause never
  flips by itself, and no stale lines, reordered digits, or neighboring pixels
  appear (`pocket-journal-cjx`, `pocket-journal-zi6`,
  `pocket-journal-e43`).
- [ ] Produce at least 30 partial updates, including Volume changes. During
  several updates, rapidly press the current control and a control that changes
  screens. The latest requested frame must win; stale taps must not affect the
  later screen; elapsed time and input must remain responsive during both
  partial and full refreshes (`pocket-journal-9by`, `pocket-journal-1vc`,
  `pocket-journal-jjt`).
- [ ] While Stopwatch runs, repeatedly toggle play/pause and confirm the icon
  and digits remain authoritative. While Timer and Interval are paused,
  repeatedly use plus/minus and confirm each displayed value is applied exactly
  once, without reverting to an older preset (`pocket-journal-8q5`,
  `pocket-journal-9by`).
- [ ] Confirm AUX short performs the current primary action, AUX double enters
  Record only from Clock or Home, and AUX hold goes Back once at 0.5 seconds.
  Playback must navigate Back from both playing and paused states; holding
  through a transition must not trigger a second action on release
  (`pocket-journal-61u`, `pocket-journal-d8j`).

No empirical panel patch alignment or size calibration is requested for this
build. Offline tests exhaustively cover byte-aligned X origins/ranges and
pixel-granular Y origins/ranges. If clean black/white-only firmware still shows
corruption, stop repeating the broad display pass and record the exact screen,
region, preceding action, and a photo; an agent will add a focused target-pattern
diagnostic before asking for calibration evidence.

### 3. Notes, Recording, Playback, And Sync

- [ ] Confirm note list rows show three notes per page with full-page navigation.
  Dates must omit the year, use the available width, remain legible, and
  truncate long note text cleanly without overlapping controls
  (`pocket-journal-nz5`, `pocket-journal-te0`).
- [ ] Record continuously for at least two minutes. Stop/save with AUX and
  confirm immediate return with no Saving screen or blocking display load.
  As soon as the durable raw note appears, enter Record again and create a
  second note while optional processing of the first continues. Exactly one
  valid playable note must result from each capture (`pocket-journal-sm1`,
  `pocket-journal-sm1.1`, `pocket-journal-5eo`).
- [ ] Play/pause both notes, navigate Back from active and paused playback, and
  play one through EOF. Confirm no square Stop or Recovered overlay, no stuck
  audio state, and no corrupt note after a reboot during or shortly after
  background processing (`pocket-journal-61u`, `pocket-journal-te0`).
- [ ] With at least two visible notes, request Sync while the companion is
  offline, then bring it online. Confirm the screen shows the note inventory
  before ACTIVE, communicates offline/active/complete truthfully, remains
  navigable, and returns to Settings after visible completion
  (`pocket-journal-kin`, `pocket-journal-kin.1`).
- [ ] During ACTIVE Sync, create or finish processing another note. Sync must
  never report false completion for the changed inventory. Confirm exactly one
  successor operation transfers the new generation, with no duplicate audio,
  transcript, or title (`pocket-journal-kin`, `pocket-journal-kin.1`,
  `pocket-journal-5eo`).

### 4. Time Apps And Alerts

- [ ] Start Interval at round 0 with the default 90-second duration. Observe at
  least two boundaries. Rounds must increment once, timing must not drift, and
  no large square Stop/INTERVAL/Recovered screen may appear. There must be no
  spontaneous or repeated beeping; reset before leaving the device
  (`pocket-journal-8q5`, `pocket-journal-xl8`).
- [ ] Start, pause, adjust, resume, leave, and revisit Stopwatch and Timer.
  Confirm large stable time, exact paused adjustments, and documented
  pause/reset/back behavior (`pocket-journal-8q5`).
- [ ] At nonzero volume, trigger one Timer or Alarm alert. Confirm one short
  nonmodal sound, responsive navigation, exact alert clearing, and audio idle
  afterward. Repeat at volume zero and confirm silence
  (`pocket-journal-oi9`, `pocket-journal-xl8`).
- [ ] Arm a short alert, enter sleep, and confirm one RTC wake without a wake
  loop. Manual wake must still work, including an early wake followed by
  re-entering sleep (`pocket-journal-54s`).

### 5. Settings, Off, And Wake

- [ ] Change Volume, Light/Dark, 12/24-hour, C/F, reading size, Alarm, Timer,
  and Interval values. Reboot and confirm every setting persists. Check the
  large day/date, temperature, humidity, battery percentage, Alarm HR/MIN
  controls, and transcript text sizing (`pocket-journal-wsl`,
  `pocket-journal-aw7`).
- [ ] On USB power, press PWR once and confirm only the splash art is rendered
  before sleep. Wake once and confirm direct return to Home, not Clock, with
  fresh time/sensor data. The first refresh and first edge/corner touch must
  work, with no double toggle (`pocket-journal-ap4.1`,
  `pocket-journal-ap4`, `pocket-journal-jjt`).
- [ ] Repeat the off/wake sequence on battery and record LED, disconnect,
  current, and wake-source behavior (`pocket-journal-ap4.1`,
  `pocket-journal-ap4`).

## Separate Physical And Perceptual Checks

These checks do not block completion of the core regression pass.

- [ ] **Audio and enclosure:** correct the microphone and speaker openings,
  then record matched speech and silence samples. Compare intelligibility,
  noise, pops, clicks, crackle, and playback loudness before detailed gain or
  filtering changes (`pocket-journal-cpk`, `pocket-journal-zon`).
- [ ] **SD recovery:** using an expendable or backed-up card, test
  remove/reinsert/remount, low/full-space behavior, and one interrupted write.
  Existing valid notes must survive, unavailable operations must fail clearly,
  and no phantom/corrupt note may appear (`pocket-journal-pc3`,
  `pocket-journal-te0`).
- [ ] **Provisioning decisions:** USB-C remains the default. BLE possession and
  recovery semantics and portal authentication/capability scope still require
  explicit product decisions (`pocket-journal-4et`, `pocket-journal-9si`).

## OTA Deferred

Do not run OTA in this batch. The previous OTA candidate predates the fixes in
`4fe641c` and is not nominated. A fresh signed candidate, manifest, digest, and
rollback plan must be prepared and identified before hardware OTA validation
(`pocket-journal-i4s`, `pocket-journal-i4s.2`). Do not create or publish a
release tag without explicit human approval.

## Result Template

For each completed or blocked section, record:

- Firmware version and exact actions.
- Expected and observed behavior.
- Board, power, enclosure, SD, and network state.
- Only the smallest relevant photo, recording, or log excerpt.
- Any clear Bead closure evidence or the narrow new defect.

Agents will reconcile the notes with Beads. Already verified behavior does not
need to be retested on unchanged firmware.
