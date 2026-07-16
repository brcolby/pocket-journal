# Human Validation Checklist

Beads is the source of truth. This is the concise operator path for one
low-frequency, large-batch hardware pass. Notes may be mildly lossy: record what
you actually observed, including blockers, and agents will map it to the exact
acceptance criteria.

Last synchronized with `bd human list`: 2026-07-15 (31 open/in-progress beads).
Do not repeat a failed check on unchanged firmware.

## Nominated Build

- Source and partner: `8472f3b`; partner package `0.1.0`.
- Ordinary fail-closed image:
  `/private/tmp/pj-final-8472f3b-ordinary/pocket_journal.bin`, 1,827,888
  bytes, SHA-256
  `1fde473d91bb33ee09adbb99096a54d87a1a17734f61fd22ad0035b67a9ad8fe`.
- Flashed signed-development factory:
  `/private/tmp/pj-final-8472f3b-signed-factory/pocket_journal.bin`,
  1,839,104 bytes, SHA-256
  `da0a6e3391635647d8d71ffee68a841fe165cdefbc4b6cd33327f4a79c4f533a`.
- Signed OTA candidate version `0.1.0`:
  `/private/tmp/pj-final-8472f3b-ota-candidate/pocket_journal.bin`,
  1,839,104 bytes, SHA-256
  `048fde91a55ec611b7faf14668dbf8a97c2fccf6da2f1db9fb8f9bbc74b477dc`.
  Its manifest SHA-256 is
  `8cd0972286d1f94f913770a2fa324ec290a8f712a2e79cf8966cbc8ea1e7099b`
  and signature SHA-256 is
  `7207b4baaecc6005b6ed081d5fd0d9f9d250ebc1709e744263df75bfb67b5bb1`.
- Trust fingerprints: P-256 manifest public DER
  `b8cd13c9dcfa943ef4804039573bee1849cb9e1158e73cebb4eb079c6bb84a2c`;
  RSA application public file
  `eeaf88a36f8add64d15160fb8c4ad2d5aa5800cb83992f99d4dfba6c915b9e86`.
- The factory image was flashed directly on 2026-07-15 without a monitor and
  without writing NVS. This is development validation, not a release; no eFuse,
  release tag, or production Secure Boot claim is involved.
- Automated evidence: full `make test` with 312 partner tests, all native/UI and
  simulator gates, `make test-simulator-runtime ui-gallery`, ordinary and signed
  ESP-IDF 6.0.1 builds, RSA image verification, P-256 canonical-manifest
  verification, and partner bundle inspection all pass.
- Live target evidence: exact `8472f3b`; USB, audio, storage, Wi-Fi, DHCP, LAN,
  mDNS, and authenticated OTA status ready; persisted UTC offset `-420`; SNTP
  synchronized and published; interval durably inactive. Five USB status
  open/close cycles took 1.39 seconds total with no holder left behind.
- The retained 993,324-byte note has SHA-256
  `5c04a1ce64a73c8c6a34da0f29ed95d48826f963f0efd4d6aaeb3f752f9dd385`.
  A full USB reprocess completed through one descriptor in 67.79 seconds versus
  the 257.09-second baseline, then released the port with no partial file. A
  subsequent device-initiated generation-4 Sync transferred this file and a new
  286,764-byte note through the isolated companion, acknowledged the exact
  generation with zero failures, and released USB. `pocket-journal-nqa` is
  closed.
- `firmware/02a.log` and the second-round notes are failure baselines from older
  `4aca539-dirty` firmware, not evidence about this nominated build.

Before starting, record board revision, enclosure state, SD identity, battery or
USB power, and approximate room conditions. Keep `idf.py monitor` closed. Before
the Sync step, ask the agent to start the bounded isolated companion; the agent
will verify no process already owns `/dev/cu.usbmodem1101`.

## One Ordered Pass

### 1. Boot, Network, And Time

- [ ] Boot once and reboot once. Confirm normal display/touch/RTC/sensor/SD/audio
  initialization, no boot loop, automatic connection to the configured network,
  and discovery as `pj-d45d34.local`. If network recovery fails, preserve only
  credential-safe phase, RSSI, channel, DHCP, reason, retry, and backoff evidence
  (`pocket-journal-d3d`).
- [ ] Confirm the displayed local clock is correct in both 12-hour and 24-hour
  modes, with no AM/PM in 12-hour mode. Start a Timer or Stopwatch before a
  reconnect/SNTP refresh; confirm its duration never jumps. Reboot and confirm
  local time remains correct (`pocket-journal-2f2`).

### 2. Display, Touch, And AUX

- [ ] Inspect Clock, Home, note lists, note playback, Record, Alarm, Stopwatch,
  Timer, Interval, Settings, Volume, and Sync. Confirm uppercase text, contiguous
  edge-to-edge controls, thick boundaries, large unclipped typography, no top
  titles or back button, three notes per page with full-page arrows, full-screen
  play/pause and recording time, and a full-height Volume bar. Confirm direct
  touch targets without a focus dot, hidden focus cycle, or inverted focus
  (`pocket-journal-nz5`, `pocket-journal-2ji`).
- [ ] Change Volume, Light/Dark, 12/24-hour, C/F, transcript size, Alarm, Timer,
  and Interval values; reboot and confirm every setting persists. Check large
  day/date, temperature, humidity, battery percentage, Alarm HR/MIN controls,
  Timer controls, and readable transcript sizing (`pocket-journal-wsl`,
  `pocket-journal-aw7`).
- [ ] Produce at least 30 partial updates across Record, Stopwatch, Timer,
  Interval, and Volume. Digits must advance in order; the bar and controls must
  never reorder, leave horizontal residue, or borrow pixels from adjacent areas.
  Full-refresh cleanup must remain coherent (`pocket-journal-cjx`,
  `pocket-journal-zi6`, `pocket-journal-e43`).
- [ ] During both a roughly 0.6-second partial and 1.8-second full refresh, use
  touch and AUX. Input and time models must continue immediately, a delayed
  gesture must not land on a later screen, and the latest frame must win without
  flicker or stale pixels (`pocket-journal-9by`, `pocket-journal-1vc`,
  `pocket-journal-jjt`).
- [ ] Confirm AUX short performs the obvious primary action, AUX double starts
  Record only from Clock or Home, and AUX hold goes Back once at 0.5 seconds.
  Holding through a transition and releasing afterward must not trigger another
  action (`pocket-journal-61u`, `pocket-journal-d8j`).

### 3. Record, Listen, Audio, And Sync

- [ ] Record for at least five seconds. Elapsed seconds must be sequential and
  based on captured audio. AUX short must stop/save and return immediately with
  no Saving screen or blocking repaint. Exactly one playable note must appear
  after asynchronous finalization; immediate Record re-entry must be rejected
  until then. Also verify one interrupted capture creates no corrupt note
  (`pocket-journal-sm1`, `pocket-journal-te0`).
- [ ] While rapidly starting/stopping Record, re-entering it, playing/stopping a
  note, navigating Back, and polling status, confirm no hang, torn/stale state,
  lost stop, or missing note refresh (`pocket-journal-5eo`).
- [ ] Play/pause a note and AUX Back from both active and paused states. No square
  Stop or recovered overlay may appear. Play through EOF and confirm the UI and
  audio hardware return to authoritative idle (`pocket-journal-61u`).
- [ ] Record one intelligible phrase and one silence/no-speech sample. Confirm
  the fixed local model produces useful readable text for speech and does not
  fabricate a transcript for noise. Preserve both samples for later enclosure
  comparison (`pocket-journal-zon`).
- [x] Agent protocol proof: the isolated companion resumed offline generation 4,
  observed active USB transfer, transferred both queued notes, and reached
  requested generation = acknowledged generation = 4 with zero failures. Both
  WAV digests verified; companion shutdown was immediate and left no USB holder
  (`pocket-journal-nqa`, closed).
- [ ] Confirm the generation-4 Sync screen visibly showed actionable offline,
  active, and complete states without blocking navigation. If those transitions
  were not observed, run one additional Sync while the companion is online
  (`pocket-journal-kin`).
- [ ] Stop the companion, request Sync again, and confirm an actionable offline
  state survives navigation and reboot. Restart the companion and confirm that
  exact operation resumes once, without duplicate audio, transcript, or title.
  Sync must never be blank or claim false progress (`pocket-journal-kin`,
  `pocket-journal-d3d`).
- [ ] Using an expendable or backed-up SD card, test removal/reinsert/remount,
  low/full-space behavior, and one interrupted write. Record and playback must
  fail clearly while unavailable, existing valid notes must survive recovery,
  and no phantom or corrupt note may appear (`pocket-journal-pc3`,
  `pocket-journal-te0`).

### 4. Time Apps And Alerts

- [ ] Start, pause, adjust, resume, leave, and revisit Stopwatch and Timer.
  Paused plus/minus controls must apply to the displayed remainder rather than
  the original preset. Time must remain stable and large; Back must perform the
  documented pause/reset/return behavior (`pocket-journal-8q5`).
- [ ] Start a 90-second Interval at round 0 and observe rounds 1 and 2. It must
  not drift, show a large square Stop/INTERVAL/Recovered screen, or emit more
  than one chime per boundary. Reboot once mid-round, verify exact recovery, then
  reset before leaving the device (`pocket-journal-8q5`, `pocket-journal-xl8`).
- [ ] At a nonzero volume, trigger one Timer or Alarm alert. Confirm one short
  nonmodal sound, exact alert clearing, responsive navigation, and PA idle after
  it. Repeat at volume zero and confirm silence. Record any pop, click, crackle,
  or harshness (`pocket-journal-oi9`, `pocket-journal-xl8`).
- [ ] Arm a short Timer, enter sleep, and confirm one RTC/GPIO5 wake with no wake
  loop. Manual BOOT/AUX wake must still work, including an early wake followed
  by re-entering sleep (`pocket-journal-54s`).

### 5. Signed OTA And Recovery

- [ ] From factory `8472f3b`, select the documented `0.1.0` candidate through
  the companion CLI. Confirm exact device/version/digest/slot preflight, explicit
  confirmation, bounded upload progress, reboot/reconnect, healthy confirmation,
  and preserved token/settings (`pocket-journal-i4s.2`, `pocket-journal-i4s`).
- [ ] Reject bad manifest and image signatures, changed fields, wrong board or
  project, digest mismatch, oversize/truncated bodies, replay/downgrade, and a
  concurrent update without changing the boot partition.
- [ ] Separately test factory-to-first-OTA behavior, interrupted upload, and an
  unconfirmed/failed first boot. The previous partition must remain bootable and
  status must report the truthful rollback or unknown outcome. Do not remove
  power during an active flash write unless that destructive test is explicitly
  accepted. Do not create or publish a release tag.

### 6. Physical Power

- [ ] On USB power, press PWR once to show only the compiled splash art and sleep.
  Press PWR once to wake directly to Home with fresh time/sensor data. The first
  full refresh and first edge/corner touch must work; held/bouncing input must not
  double-toggle. A recoverable display failure must force a truthful later full
  refresh (`pocket-journal-ap4.1`, `pocket-journal-ap4`,
  `pocket-journal-jjt`, `pocket-journal-1vc`).
- [ ] Repeat the same off/wake sequence on battery and record current, LED,
  disconnect, and wake-source behavior (`pocket-journal-ap4.1`,
  `pocket-journal-ap4`).

## Separate Decisions And Physical Work

These do not interrupt the consolidated core pass.

- [ ] Choose optional BLE possession, provisioning entry/timeout, replacement,
  recovery, rate-limit, and clearing semantics. USB-C remains the default
  (`pocket-journal-4et`; implementation remains `pocket-journal-db1`).
- [ ] Approve same-origin portal session/authentication, CSRF, revision, and
  capability scope before portal work (`pocket-journal-9si`; implementation
  remains `pocket-journal-kky`).
- [ ] Correct enclosure microphone and speaker openings, then capture controlled
  before/after audio before detailed gain, filtering, crackle, or loudness tuning
  (`pocket-journal-cpk`).

## Result Template

For each checked or blocked section, record the firmware version, exact actions,
expected and observed behavior, board/power/enclosure state, and only the small
relevant log/photo/recording excerpt. State any obvious bead that can close or
the narrow follow-up defect. Agents will reconcile Beads and this file; unchanged
verified behavior is not retested.
