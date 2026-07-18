# Human Validation Checklist

Beads is the source of truth. This file is the operator path for one
low-frequency, large-batch hardware pass. Human notes may be mildly lossy:
record what you actually observed, including blockers, and agents will map that
evidence to the exact Bead acceptance criteria.

Last synchronized with `bd human list`: 2026-07-17 (35 human-validation Beads).
Do not repeat a failed check on unchanged firmware.

## Nominated Build

- Source commit and reported firmware version: `f57d49b` / `f57d49b-dirty`.
  The dirty suffix is caused by preserved unrelated local partner/documentation
  changes; the firmware sources are committed through `f57d49b`.
- Image: `firmware/build-carbon-fixes/pocket_journal.bin`.
- Size: 1,511,424 bytes (`0x171000`).
- SHA-256:
  `581be34f098f43f671de9dc22b8ea31843aa08934519ad147af7661e692a9f28`.
- ESP image validation hash:
  `7657b851b3bae085f0d8e51a67caab07f393913bb0ebe90970a5d47cd63c34c4`.
- Built with ESP-IDF 6.0.1 and 28% (`0x8f000`) free in each 2 MiB
  application partition. A clean build contains no LVGL component or symbol.
- Flashed and byte-verified as a development image on `pj-d45d34`, ESP32-S3
  MAC `14:C1:9F:D4:5D:34`, at `/dev/cu.usbmodem1101`.
- Live no-reset probe after flashing: exact firmware answered `PJ_STATUS` with
  boot ID `1779544349`; SD storage, audio, Wi-Fi/DHCP, and fixed-offset time
  synchronization were ready. USB recovery was neither needed nor attempted.
- Automated evidence: deterministic Carbon generation verified 73 active and
  26 reference sources, 72 glyph identities, and 30 semantic bitmaps; all 13
  asset and 7 exhaustive DXF tests passed. All 32 native C executables and 344
  Python cases passed, including 328 partner tests. Combined ASan/UBSan
  UI/worker/pipeline/refresh runs, WASM runtime, one-bit/AUX simulator tests,
  exact Timer partial reconstruction, and image analysis passed. All 51
  gallery frames and the contact sheet were inspected.
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

### 2. Carbon Display, DXF Touch, Cadence, And AUX

For machine-readable evidence, capture the serial monitor while performing this
section (stop with Ctrl-]):

```bash
cd firmware
source "$HOME/.espressif/v6.0.1/esp-idf/export.sh"
idf.py -B build-carbon-fixes -p /dev/cu.usbmodem1101 monitor \
  --no-reset --timestamps --disable-auto-color 2>&1 | \
  tee /private/tmp/pj-carbon-hardware.log
```

Afterward, preserve the output of:

```bash
rg 'Seconds cadence (start|end)|Display metrics|Display generations' \
  /private/tmp/pj-carbon-hardware.log
```

- [ ] Exercise every sector of Home `3_1`, Notes `3_1m`, Time `4_1`, and
  Settings `4_0m`, including screen edges, both sides of every visible divider,
  the four-sector center diagonal, and all divider intersections. On Alarm,
  Stopwatch, Timer, and Interval, exercise the active rectangular hitboxes even
  though their separators are intentionally invisible. Each touch must have
  stable single-slot ownership, with no dead strip or mirrored icon
  (`pocket-journal-nz5`, `pocket-journal-nz5.5`).
- [ ] Inspect Clock, Home, Notes, note lists/detail, Record, Alarm, Stopwatch,
  Timer, Interval, Settings, Volume, and Sync in light and dark themes. Confirm
  case-preserving Carbon letters/numbers, distinct upright `1` and `9`, bold
  mapped icons at a consistent scale, 4 px interior rules with no redundant
  outer box, the unfilled Home Time icon, compact lowercase `12h`/`24h`,
  legible note paging, and a centered three-line Sync stack using one font
  size. Confirm Volume shows only a 0–10 number above its controls and no
  square Stop icon appears anywhere (`pocket-journal-nz5`,
  `pocket-journal-nz5.6`, `pocket-journal-nz5.8`, `pocket-journal-2ji`).
- [ ] Run Record, Stopwatch, Timer, and Interval for at least 120 consecutive
  displayed seconds each. Record must first present a complete `00:00` screen,
  then measure playable captured PCM duration. For every clock, observe each
  second exactly once with no skip, duplicate, late stall, or superseded frame;
  exercise `08 -> 09 -> 10`, `11 -> 12 -> 13`, `19 -> 20`, `39 -> 40`, and
  `59 -> 60`, Play/Pause, and Interval round boundaries. Digits must remain
  complete, with no old/new segments interleaved. The log must contain one
  named cadence start/end per run, at least 120 submitted sequences,
  `late_max_ms <= 75`, and zero overruns and misses (`pocket-journal-cjx`,
  `pocket-journal-zi6`, `pocket-journal-9by`, `pocket-journal-nz5.8`).
- [ ] Keep one seconds clock active for at least 30 partial updates. Confirm no
  cleanup full refresh interrupts the cadence; pause or leave afterward and
  confirm exactly one safe deferred cleanup. The log must show cleanup deferred
  while active and promoted on cadence end. Across this and Volume number
  changes, confirm no ghosting, reordered digits, displaced pixels, or stale
  neighboring content (`pocket-journal-e43`, `pocket-journal-9by`).
- [ ] In Settings, switch 12h/24h and confirm only the localized content changes
  via partial refresh. Switch theme and confirm an immediate full inversion;
  the top-right `AsleepFilled` icon must retain the same shape. Battery changes,
  Clock/status ticks, Alarm Toggle, Volume, Play/Pause, and other same-layout
  changes must remain exact partials (`pocket-journal-nz5`,
  `pocket-journal-cjx`, `pocket-journal-e43`).
- [ ] During several partials, rapidly press the current control and then one
  that changes screens. The latest accepted frame must win, a rejected request
  must retry losslessly, and stale taps must not affect the later screen. While
  Stopwatch runs, repeatedly toggle Play/Pause; while Timer and Interval are
  paused, use their carets and confirm each authoritative value is applied once
  without reverting (`pocket-journal-9by`, `pocket-journal-1vc`,
  `pocket-journal-8q5`, `pocket-journal-jjt`).
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

- [ ] Confirm note list rows show three notes per page with Carbon chevrons for
  full-page navigation. Dates must omit the year, use the available width,
  contain no spaces (for example `JUL1709:09`), remain legible, and truncate
  long note text cleanly without overlapping controls (`pocket-journal-nz5`,
  `pocket-journal-nz5.8`, `pocket-journal-te0`).
- [ ] Record continuously for at least two minutes. Stop/save with AUX and
  confirm immediate return with no Saving screen or blocking display load.
  As soon as the durable raw note appears, enter Record again and create a
  second note while optional processing of the first continues. Exactly one
  valid playable note must result from each capture (`pocket-journal-sm1`,
  `pocket-journal-sm1.1`, `pocket-journal-5eo`).
- [ ] Play/pause both notes, navigate Back from active and paused playback, and
  play one through EOF. Confirm the compact Play/Pause control leaves no heavy
  ghost, no square Stop or Recovered overlay appears, and there is no stuck
  audio state or corrupt note after a reboot during or shortly after
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

- [ ] Start Interval at round 0 with the default 60-second duration. Adjust Down
  once to the 30-second minimum, then Up once to 60 seconds; each step must be
  exactly 30 seconds. Observe at least two boundaries. The round counter must
  use the same type size as the duration when it fits, rounds must increment
  once, timing must not drift, and no large square Stop/INTERVAL/Recovered
  screen may appear. There must be no spontaneous or repeated beeping; reset
  before leaving the device
  (`pocket-journal-8q5`, `pocket-journal-xl8`).
- [ ] Start, pause, adjust, resume, leave, and revisit Stopwatch and Timer.
  From `00:30`, press Timer Up three times and confirm the exact visible series
  `01:00`, `01:30`, `02:00`; press Down three times and confirm the reverse.
  Confirm large stable time and documented pause/reset/back behavior
  (`pocket-journal-8q5`, `pocket-journal-nz5.8`).
- [ ] At nonzero volume, trigger one Timer or Alarm alert. Confirm one short
  nonmodal sound, responsive navigation, exact alert clearing, and audio idle
  afterward. Repeat at volume zero and confirm silence. At volume 10, confirm
  the requested approximately 2x output is useful without clipping, crackle,
  or speaker distress (`pocket-journal-oi9`, `pocket-journal-xl8`,
  `pocket-journal-nz5.8`).
- [ ] Arm a short alert, enter sleep, and confirm one RTC wake without a wake
  loop. Manual wake must still work, including an early wake followed by
  re-entering sleep (`pocket-journal-54s`).

### 5. Settings, Off, And Wake

- [ ] Change Volume, theme, and 12/24-hour mode through the fixed Settings
  sectors; change C/F, reading size, Alarm, Timer, and Interval through their
  supported control or companion path. Reboot and confirm every setting
  persists. Check the large day/date, temperature, humidity, 28 px battery icon
  plus percentage, Alarm caret controls, and transcript text sizing
  (`pocket-journal-wsl`, `pocket-journal-aw7`).
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

Do not run OTA in this batch. Existing OTA candidates predate the Carbon build
`574332a` and are not nominated. A fresh signed candidate, manifest, digest, and
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
