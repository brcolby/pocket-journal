# Device API

The ESP32 exposes a versioned LAN API after USB-C or BLE provisioning and Wi-Fi connection.

All requests use a per-device bearer token created during provisioning:

```http
Authorization: Bearer <pairing-token>
```

TLS is intentionally deferred for v1 LAN derisking.

Every `/v1` route is protected. Missing, malformed, or invalid credentials receive `401 Unauthorized`, a `WWW-Authenticate: Bearer realm="pocket-journal"` header, and the same non-secret JSON error. Provisioning is performed outside the LAN API over the dedicated USB-C or BLE transports; there is no unauthenticated LAN health endpoint.

## Endpoints

```http
GET /v1/status
```

Returns device id, firmware version, board profile, Wi-Fi state, storage state, battery state if available, temperature, relative humidity, pending sync counts, and the latest recording-wipe operation. Environmental readings use `temperature_c` and `humidity_percent`; humidity is `null` until the sensor returns a CRC-valid sample. Unit conversion is a presentation preference and does not alter the canonical Celsius value.

```json
{
  "pending_sync": 2,
  "transferred_sync": 4,
  "recording_wipe": {
    "id": 17,
    "state": "running",
    "audio_deleted": 3,
    "transcripts_deleted": 2,
    "notes_deleted": 2,
    "code": null,
    "retryable": false
  }
}
```

Wipe states are `idle`, `queued`, `running`, `succeeded`, and `failed`. The idle operation has id `0`. `recording_wipe_recent` retains a bounded newest-first history of terminal operations so a client can still resolve its operation id if a later wipe starts before the next poll. During exclusive storage maintenance, status remains responsive and reports the last cached sync counts instead of walking the card.

The time/temp screen can display battery percentage when firmware can read it from the board power-management path. Until hardware bring-up confirms that path, simulator and native tests use a dummy percentage.

```http
GET /v1/time
PUT /v1/time
```

Reads or updates the local time/date shown on the time/temp screen. The firmware
accepts integer `hour`, `minute`, `month`, and `day` fields, plus optional `year`
for weekday calculation and `utc_offset_minutes` from `-840` through `840` for
projecting SNTP UTC into the same local civil time and RTC basis. Omitting the
offset preserves compatibility with older callers.

```json
{
  "hour": 14,
  "minute": 5,
  "year": 2026,
  "month": 6,
  "day": 19,
  "utc_offset_minutes": -420
}
```

```http
GET /v1/settings
PUT /v1/settings
```

Reads or atomically updates the persisted settings surface. A PUT may contain any non-empty subset of these fields; an unknown, incorrectly typed, or out-of-range field rejects the entire update without replacing the last valid settings:

```json
{
  "theme": "dark",
  "volume": 8,
  "alarm_enabled": true,
  "alarm_hour": 7,
  "alarm_minute": 30,
  "timer_seconds": 300,
  "interval_seconds": 90,
  "clock_24h": true,
  "temperature_unit": "c",
  "transcript_font_size": 3,
  "expected_generation": 12
}
```

`theme` is `light` or `dark`; `volume` is `0` through `10`; alarm time uses a 24-hour stored value; `timer_seconds` is `30` through `86400`; `interval_seconds` is `60` through `86400`; `clock_24h` is boolean; `temperature_unit` is `c` or `f`; and `transcript_font_size` is `2` or `3`. GET also returns the derived `sync_pending` and `sync_transferred` counters plus a monotonic `generation`. The partner includes that value as `expected_generation`; a stale value returns `409 Conflict` without partial mutation. Legacy callers may omit it, but generation pinning is required for conflict-safe writes. When NVS has no valid stored value, defaults preserve full codec volume (`10`), light mode, a disabled `07:30` alarm, a five-minute timer, a 90-second interval, 24-hour time, Celsius, and the larger transcript font. Firmware settings schema 2 also migrates the former stored 1500-second interval default to 90 seconds.

```http
GET /v1/audio
GET /v1/audio/{audio_id}
DELETE /v1/audio
```

Lists, downloads, or wipes retained WAV recordings from the TF card. List items include `created_at`, `duration_ms`, `synced`, and `transcript_path`; legacy WAVs receive deterministic metadata derived from their filename and header.

`DELETE /v1/audio` starts an asynchronous, exclusive maintenance operation that removes audio, transcript, and note metadata files. It promptly returns `202 Accepted`; a duplicate start while the wipe is queued or running attaches to the same operation.

```json
{
  "accepted": true,
  "attached": false,
  "recording_wipe": {
    "id": 17,
    "state": "queued",
    "audio_deleted": 0,
    "transcripts_deleted": 0,
    "notes_deleted": 0,
    "code": null,
    "retryable": false
  }
}
```

Poll `GET /v1/status` until the matching operation id reaches `succeeded` or `failed`. A failed operation reports a stable code such as `wipe_incomplete` and a `retryable` boolean. Start requests return `409 Conflict` with a credential-safe `code` when audio or another storage user is active, and `503 Service Unavailable` when storage is unavailable or the worker cannot start.

Recording, playback, directory enumeration, audio downloads, transcript/metadata updates, storage recovery, and light-sleep admission participate in the same shared/exclusive coordinator. Light sleep is deferred while storage work is active. If a FAT/VFS call does not return, firmware does not delete or cancel the worker task: the worker remains quarantined as the exclusive storage owner, status continues from cached data, and new storage work plus recovery is rejected. Reset the device to recover from a permanently stuck driver call; recovery is allowed only after the worker reaches a terminal state.

```http
PUT /v1/transcripts/{audio_id}
```

Uploads a JSON transcription containing non-empty `text` for an existing recording. A successful upload atomically stores the transcript and marks the note synced; the READ view is populated from these transcript records.

```http
GET /v1/ota
POST /v1/ota/preflight
POST /v1/ota
```

The authenticated OTA status reports the running and target versions, digest,
running/target partition address and subtype, `running_slot`/`target_slot`,
`target_partition_matches`, transferred bytes, `reboot_required`, and
`boot_outcome`. It also exposes whether PSA crypto initialization, rollback, the
trusted manifest key, ESP-IDF signed-app verification, and the boot-time
manifest-signature self-test are ready. `boot_outcome` is one of `not_started`, `awaiting_reboot`,
`testing`, `confirmed`, `rollback_requested`, `rolled_back`, or `failed`.

Preflight accepts an exact JSON object containing `size`, lowercase `sha256`,
`project`, `board`, `target`, `version`, `secure_version`, and an ECDSA DER
`signature` encoded as hex. The signature covers these canonical
ASCII bytes:

```text
PJOTA1
sha256=<sha256>
size=<decimal bytes>
project=<project>
board=<board>
target=<target>
version=<version>
secure_version=<decimal>
```

The final newline is part of the signed message. Unknown, missing, duplicate,
malformed, mismatched, replayed, downgraded, or unsigned manifests are rejected.
A non-semver factory/development version may migrate once to an `X.Y.Z`
candidate. After that transition both versions must be strict comparable semver;
semver-to-nonsemver and nonsemver-to-nonsemver updates are rejected.

An accepted preflight returns a random `upload_id`, exact device id, versions,
and size. Preflight is advisory and does not reserve flash/storage mutations. A
later valid preflight atomically supersedes an unused READY preflight, so a
cancelled CLI prompt or lost response does not strand OTA. Only an active write
or a pending reboot is exclusive.

Upload the exact image with `Content-Type: application/octet-stream`, the
preflight size as `Content-Length`, and these headers:

```http
X-PJ-Upload-ID: <upload_id>
X-PJ-Image-SHA256: <sha256>
X-PJ-Activate: true
```

The device atomically excludes recording, playback, processing, wipe, recovery,
sleep, and other storage mutations at upload start. A busy device returns
`409 ota_busy` without releasing the active upload's reservation. The bounded
stream is written only to the inactive OTA partition, hashed while writing,
validated as an ESP32-S3 application, checked against the signed descriptor and
secure version, and accepted by ESP-IDF signed-app verification before the boot
partition changes. Interrupted or rejected uploads abort without activation.
The device arms its bounded restart task before returning `202 Accepted`. If
that task cannot be created, it restarts synchronously; if response delivery
fails, the already-armed task still restarts the device. The exclusive mutation
lease therefore remains held from verified activation until reset.

Activation records the exact target partition address/subtype as well as version
and digest. On reboot, both version and running partition must match. Before
health can be accepted, `pending_reboot` is durably changed to `testing`; a
reboot that finds `testing` plus IDF `PENDING_VERIFY` treats the prior health
attempt as interrupted and forces rollback. Firmware is confirmed only after
required services/tasks, UI initialization, and the first display flush
succeed. A torn post-health NVS write is reconciled only when the exact image is
already IDF `VALID` and the durable record is nonterminal. `failed_health`,
rollback, failed, and unknown records never promote an image, even if a later
boot appears healthy. Terminal-marker writes and rollback failures use bounded,
backed-off retries; NVS is not rewritten after the marker first persists. LAN
OTA preflight and upload remain blocked while verification or terminal recovery
is unresolved. Authentication reads the current provisioned token for every
request and fails closed before provisioning.

## USB-C Partner Commands

The firmware also accepts a small line protocol on the USB Serial/JTAG console for maintenance when Wi-Fi is unavailable. The firmware console must be configured with USB Serial/JTAG as the primary console input; a secondary USB log console is output-only for `stdin` consumers.

```text
PJ_STATUS [request_id=ID]
PJ_WIFI_HEX 4c61622057694669 70617373776f7264 746f6b656e
PJ_TIME 2026 06 20 14 05 -420
PJ_WIPE_RECORDINGS [request_id=ID]
PJ_SETTINGS_GET request_id=ID
PJ_SETTINGS_SET expected_generation=N payload_hex=JSON_HEX request_id=ID
PJ_AUDIO_LIST cursor=0 snapshot=0 [request_id=ID]
PJ_AUDIO_READ id_hex=ID offset=0 max_bytes=256 [source_sha256=SHA256] [request_id=ID]
PJ_TRANSCRIPT_BEGIN id_hex=ID bytes=N sha256=SHA256 request_id=ID
PJ_TRANSCRIPT_WRITE upload_id=ID offset=N data_hex=DATA request_id=ID
PJ_TRANSCRIPT_COMMIT upload_id=ID sha256=SHA256 request_id=ID
PJ_TRANSCRIPT_ABORT upload_id=ID request_id=ID
PJ_AUDIO_TONE [0|1|-] [dout_gpio] [pa=0|1|-] [dout=gpio] [pwr=0|1|-] [gpio44=0x00..0xff] [gp45=0x00..0xff]
PJ_MIC_CHECK [duration_ms] [ms=1..10000] [gain_db=0..42]
```

`PJ_WIFI_HEX` stores hex-encoded UTF-8 `ssid`, `password`, and bearer-token strings in NVS. It intentionally does not echo credentials back over serial.
`PJ_TIME` and its `PJ_SET_TIME` alias accept an optional sixth fixed UTC-offset
field in minutes, from `-840` through `840`. When supplied, firmware persists
the offset and uses it to project later background SNTP UTC results into the
local civil clock and PCF85063 RTC. The legacy five-field form remains valid
and leaves any saved offset unchanged. A fixed offset does not automatically
track daylight-saving transitions; the partner should resend time when the
host offset changes.
`PJ_AUDIO_TONE` plays a generated diagnostic tone. Its optional first argument forces the speaker PA GPIO level; `-` keeps the firmware default. Its optional second argument temporarily routes I2S TX data to a DOUT GPIO for board pin-map diagnosis. The named arguments expose the same probes plus temporary audio power GPIO and ES8311 register overrides; the firmware restores the normal route, power level, and register values after the tone.
`PJ_MIC_CHECK` samples the ES8311 microphone path without creating a note file and returns input statistics: `peak`, `avg_abs`, `clipped`, `near_zero`, `read_errors`, and `silent`. The production recording gain is `42 dB`; use lower diagnostic overrides only when measuring input headroom.

`PJ_SETTINGS_GET` returns the same canonical settings fields and generation as
the LAN endpoint. `PJ_SETTINGS_SET` carries a non-empty partial update as at most
256 decoded bytes of compact, hex-encoded JSON and requires the generation read
immediately before it. Unknown, removed, duplicate, malformed, oversized, or
stale updates fail without mutation. Retrying the same request id with identical
generation and payload returns success after a lost acknowledgment; changing the
content under a reused request id is rejected.

The note-transfer commands are line bounded. All identifiers, filenames, labels,
timestamps, transcript paths, and binary chunks are lowercase hex encoding of
their UTF-8/raw bytes. Current audio identifiers are recording filenames and
decode to at most 95 bytes; the partner bounds other decoded text to 160 bytes.
Each audio-read chunk is at most 256 bytes, each transcript-write chunk is at
most 192 bytes, and a transcript is at most 65536 bytes. The smaller write bound
keeps the hex-encoded command plus a 32-character request id below the client's
conservative 512-byte compatibility envelope. Current firmware may accept a
256-byte write chunk, but the partner emits at most 192 bytes.

`PJ_AUDIO_LIST` returns one item at a time. The first request uses `snapshot=0`;
the response creates a positive snapshot id. Every subsequent request echoes it.
A response has `snapshot`, `cursor`, `next_cursor`, and `done`. A non-empty
response also has one `item` with `audio_id_hex`, `filename_hex`, optional
`label_hex`, `created_at_hex`, `transcript_path_hex`, `size`, `data_bytes`,
`duration_ms`, `source_sha256`, `synced`, and `transcript_uploaded`. `done` is
true on the response carrying the final item; only an empty list has `done: true`
without an item. The firmware returns a retryable `list_changed` error instead
of mixing generations.

`PJ_AUDIO_READ` returns `id_hex`, `offset`, `total_bytes`, `data_hex`, `eof`, and
the required source SHA-256. The response offset must equal the requested
offset, total length and digest remain invariant across chunks, and EOF is true
exactly when the returned bytes end at `total_bytes`. When supplied, the request
digest rejects a recording that changed after listing.

Transcript upload is a staged transaction. A new `PJ_TRANSCRIPT_BEGIN` returns a
positive `upload_id`, `offset: 0`, and `accepted: true`; an idempotent replay also
sets `attached: true` and reports the current offset. Each write returns that id
plus its exact `next_offset`. Commit succeeds only when byte count and SHA-256
match the begin declaration, then atomically replaces the transcript and marks
the associated note synced; it returns `committed: true` and `bytes`. Abort is
safe to repeat. Begin, repeated identical writes, commit, and abort must be
idempotent for the same request/upload identity so a lost serial acknowledgment
does not duplicate or corrupt state.

Responses start with `PJ_OK` or `PJ_ERR` followed by a compact JSON object. Normal ESP-IDF logs may appear on the same serial stream, so clients should scan for those prefixes.

`PJ_STATUS`, `PJ_WIPE_RECORDINGS`, `PJ_AUDIO_LIST`, and `PJ_AUDIO_READ` accept an optional 1-32 character request id containing letters, digits, `.`, `_`, or `-`; settings mutations and staged transcript commands require one. The partner tags every mutation and transfer request. Tagged responses include both `command` and `request_id`; clients must reject a response when either differs from the request. The partner CLI retries a lost wipe-start, settings mutation, or transfer acknowledgment with the same request id, allowing firmware to return the associated operation. It uses a newly opened, bounded-lifetime serial descriptor for requests and never retains the port while idle. A wipe timeout identifies the still-running operation id so a later status check can determine the outcome.
