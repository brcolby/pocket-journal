# Time alert state model

`pj_time_model` is the platform-neutral source of truth for alarm, timer,
interval, stopwatch, snooze, recovery, and alert ordering state. The current UI
fields can be treated as a projection of this model during board integration;
screen navigation must not start, stop, or reset timekeeping.

## Clock contract

Every operation receives a `pj_time_clock_t` snapshot:

- `boot_id` changes on every boot and `monotonic_ms` never moves backward within
  one boot.
- `wall_utc_ms` is persisted for diagnostics only. Duration math never uses it.
- `local_day` and `local_second` are civil local time inputs used only to find a
  daily alarm crossing. The platform owns timezone and daylight-saving rules.
- On the first call after a reboot, `reboot_elapsed_valid` may provide elapsed
  sleep/off time from a trusted RTC or wake source. When it is unavailable,
  running duration state is preserved at its last checkpoint, not guessed from
  a potentially changed wall clock, and `recovery_time_uncertain` is latched.
  After that condition has been shown or logged, acknowledge it explicitly with
  `pj_time_recovery_acknowledge`.

Call `pj_time_advance` before displaying state, immediately before every
mutation that will be persisted, and after wake. This keeps all running anchors
aligned with the durable-record time, which is the reference for
`reboot_elapsed_ms`. It returns nonzero
when any checkpoint or alert state changed. Persist after every successful
mutation and nonzero advance. The integration should checkpoint periodically while a clock is running
so an untrusted hard-power-loss loses at most that checkpoint interval.

## Alarm and expiry recovery

A configured alarm establishes the current local minute as its baseline, so
enabling an alarm after its time does not immediately produce a stale alert.
Crossing the configured minute produces one occurrence keyed by configuration
generation and local day. The persisted occurrence is a per-generation
high-water mark, so a multi-day civil-time rewind cannot replay an older day
when it is crossed again. A forward jump or reboot that crosses the minute
produces a recovered alert for the most recent crossed occurrence.

Timer and snooze deadlines use remaining duration plus a monotonic anchor.
Intervals use the same anchor, advance through every crossed work/rest phase,
and coalesce multiple boundaries into one recovery alert whose
`skipped_occurrences` reports the additional boundaries. Stopwatch state is an
accumulated duration plus a monotonic running anchor.

Pausing an interval checkpoints its current phase and remaining duration.
`pj_time_interval_resume` anchors that same phase at the supplied monotonic
clock; it does not reset the work/rest cycle and time spent paused, including a
reboot, is not charged to the interval.

The active alert and every queued alert have persisted, increasing identities.
An alert remains active across reboot until its exact ID is dismissed or, for
an alarm, snoozed. Re-evaluating the same state cannot issue the expiry twice.
If another occurrence of a source arrives while that source is still active or
queued, it is coalesced into the existing identity and increments the skipped
count rather than overflowing the bounded queue.
Starting, resetting, or reconfiguring a source acknowledges an older alert from
that same source; explicit dismiss and snooze operations require the current
persisted alert ID.

Countdown, snooze, and individual interval phase durations must be nonzero and
no longer than 30 days. The current UI presets are substantially narrower.

## Priority and media conflicts

Simultaneous alerts are presented in this fixed order:

1. daily alarm or snoozed alarm
2. countdown timer
3. interval boundary

FIFO identity order breaks ties. A higher-priority arrival preempts a visible
lower-priority alert, which returns to the queue. Ordinary playback is stopped
before any alert is presented. Recording is never destroyed by an alert: show
the visual alert immediately and defer its audio until recording has ended.
The board layer should wake the display when hardware permits and apply the
user's alert volume policy when audio presentation begins.

## Durable record

`pj_time_state_encode` writes a 512-byte, zero-filled, little-endian record:

- bytes 0-3: `PJTM` magic
- byte 4: format version (`1`)
- canonical alarm, countdown, interval, stopwatch, active-alert, and queue data
- bytes 508-511: CRC-32 of bytes 0-507

Decoding requires the exact record size, known version, valid CRC, and valid
field ranges. Persistence should use the repository's atomic NVS or file update
pattern; a failed decode falls back to defaults and should be surfaced as a
storage/recovery diagnostic rather than partially accepting state.
