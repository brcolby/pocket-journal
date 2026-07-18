# Human Validation Checklist

Beads is the source of truth. This file lists only active human decisions and
physical or perceptual checks.

Last synchronized with active `human` labels: 2026-07-18 (11 Beads).

## Accepted On 2026-07-18

The product owner approved closure of the grouped display/UI, recording,
playback, sync, transcription, non-alert time, settings, and persistence Beads.
Display issues were accepted as approximately 95% resolved; any concrete
regression should be reopened or filed as a focused Bead.

## Power And Sleep

Active Beads: `pocket-journal-ap4`, `pocket-journal-ap4.1`.

The prior `f3492f8` development image wakes to Home and is not evidence for the
corrected contract. Build and flash an identified image containing the
2026-07-18 power-wake change before running this section.

- [ ] On USB power, press PWR once and confirm only the compiled splash appears
  before bounded light sleep. Press PWR again and confirm direct return to the
  Clock screen, not Home, with refreshed time and sensor data.
- [ ] Repeat on battery power. Confirm one action per press, no held-button or
  bounce loop, working first refresh/touch, and coherent alert wake sources.

## Alerts And RTC Wake

Active Beads: `pocket-journal-54s`, `pocket-journal-oi9`,
`pocket-journal-xl8`.

- [ ] At nonzero volume, trigger one Timer or Alarm alert. Confirm one short
  nonmodal sound, responsive navigation, exact alert clearing, and audio idle
  afterward.
- [ ] Repeat at volume zero and confirm silence. At maximum volume, confirm
  useful output without clipping, crackle, harshness, or speaker distress.
- [ ] Arm a short alert, enter sleep, and confirm one RTC wake without a wake
  loop. Confirm manual PWR wake still works, including an early manual wake
  followed by re-entering sleep.

## Enclosure Acoustics

Active Bead: `pocket-journal-cpk`.

- [ ] Approve the dimensioned microphone and speaker opening plan before any
  irreversible enclosure change.
- [ ] Install the corrected enclosure and capture controlled before/after
  speech, silence, and playback evidence. Confirm the ports are unobstructed
  and enclosure muffling, crackle, rubbing, buzz, and resonance are acceptably
  reduced.

## SD Recovery

Active Bead: `pocket-journal-pc3`.

- [ ] Using an expendable or backed-up card, test removal/reinsertion, remount,
  low/full-space behavior, and one interrupted write. Existing valid notes must
  survive, unavailable operations must fail clearly, and no phantom or corrupt
  note may appear.

## Product Decisions

Active Beads: `pocket-journal-4et`, `pocket-journal-9si`.

- [ ] Choose BLE possession authentication, provisioning entry/timeout,
  replacement, recovery, rate limiting, and clearing behavior.
- [ ] Choose portal authentication/session/CSRF behavior and approve the exact
  portal capability inventory.

## OTA Deferred

Active Beads: `pocket-journal-i4s`, `pocket-journal-i4s.2`.

Do not run OTA using the stale pre-Carbon candidate. Prepare and identify a
fresh signed factory/candidate pair, manifest, hashes, and rollback plan first.
Hardware acceptance requires authenticated upload, reboot/reconnect, first-OTA
migration, interrupted upload safety, and rollback after forced failure,
reset, crash/watchdog, and power interruption. Creating or publishing a release
tag still requires explicit human approval.
