# Storage Recovery

Pocket Journal keeps valid recordings readable when the microSD card is low on
space or contains unrelated corrupt files. New writes preserve a 256 KiB free
space reserve so FAT metadata and an in-progress file can still be finalized.

## Status Contract

Authenticated `GET /v1/status` includes:

- `storage_mounted`: whether the FAT filesystem is mounted.
- `storage_health`: `healthy`, `low_space`, `full`, `io_error`, or `unmounted`.
- `storage_total_bytes` and `storage_free_bytes`: the most recent FAT capacity
  reading.
- `storage_recovery_count`: interrupted temporary or backup artifacts repaired
  or removed since boot.
- `last_error`: an actionable failure, such as checking the card/filesystem,
  freeing space, or invoking storage recovery.

`full` is intentionally a write restriction, not a read failure. Valid notes
remain enumerable and downloadable. Recording, transcript, and static-art
writes fail before consuming the reserve. A runtime capacity check covers each
next 64 KiB recording interval; an out-of-space capture is removed instead of
being published as a valid note.

## Interrupted And Corrupt Files

Recordings are captured as `*.wav.tmp` and published only after the WAV header,
audio processing, file flush, and file sync succeed. Stale recording temps are
removed at mount. Enumeration strictly validates PCM WAV structure, sample
format, and that RIFF and data sizes exactly account for the file, so a corrupt,
truncated, or trailing-data WAV is ignored without affecting other notes.

Metadata and transcript JSON updates use `*.tmp` and preserve the previous file
as `*.bak` during replacement. On the next mount:

- stale `*.tmp` files are removed;
- a `*.bak` is restored when its destination is absent;
- a `*.bak` is removed when the destination was already committed.

Malformed metadata is ignored and regenerated from its valid WAV. Malformed or
empty transcript JSON is ignored and leaves that note pending sync.

## Remount Without Reboot

After reinserting or repairing a card, call authenticated
`POST /v1/storage/recover`. Recovery is rejected with `409 Conflict` while
recording, playback, or audio processing is active. Otherwise the firmware
unmounts the existing FAT registration if needed, remounts, performs interrupted
write cleanup, refreshes capacity, reloads static art, and refreshes notes.

Hardware validation still required:

1. Remove and reinsert the card, call the recovery endpoint, and confirm notes
   return without reboot.
2. Fill a FAT32/exFAT card to each threshold and confirm valid recordings remain
   readable while new writes receive the documented error.
3. Interrupt power during recording and transcript replacement, then confirm
   boot cleanup preserves every previously committed note.
