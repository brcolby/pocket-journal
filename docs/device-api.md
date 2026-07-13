# Device API

The ESP32 exposes a versioned LAN API after BLE provisioning and Wi-Fi connection.

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

Returns device id, firmware version, board profile, Wi-Fi state, storage state, battery state if available, temperature, relative humidity, and pending sync counts. Environmental readings use `temperature_c` and `humidity_percent`; humidity is `null` until the sensor returns a CRC-valid sample. Unit conversion is a presentation preference and does not alter the canonical Celsius value.

The time/temp screen can display battery percentage when firmware can read it from the board power-management path. Until hardware bring-up confirms that path, simulator and native tests use a dummy percentage.

```http
GET /v1/time
PUT /v1/time
```

Reads or updates the local time/date shown on the time/temp screen. The v0 firmware accepts integer `hour`, `minute`, `month`, and `day` fields, plus optional `year` for weekday calculation.

```json
{
  "hour": 14,
  "minute": 5,
  "year": 2026,
  "month": 6,
  "day": 19
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
  "transcript_font_size": 3
}
```

`theme` is `light` or `dark`; `volume` is `0` through `10`; alarm time uses a 24-hour stored value; `timer_seconds` is `30` through `86400`; `interval_seconds` is `60` through `86400`; `clock_24h` is boolean; `temperature_unit` is `c` or `f`; and `transcript_font_size` is `2` or `3`. GET also returns the derived `sync_pending` and `sync_transferred` counters. When NVS has no valid stored value, defaults preserve full codec volume (`10`), light mode, a disabled `07:30` alarm, a five-minute timer, a 90-second interval, 24-hour time, Celsius, and the larger transcript font. Firmware settings schema 2 also migrates the former stored 1500-second interval default to 90 seconds.

```http
GET /v1/home
PUT /v1/home
```

Reads or updates the custom home screen design supplied by the partner app. The device should retain up to four slots.

```json
{
  "title": "Pocket Journal",
  "slots": [
    {"label": "Notes", "icon": "stylus_note", "state": "notes"},
    {"label": "Sync", "icon": "sync", "state": "sync"}
  ]
}
```

```http
GET /v1/static-art
PUT /v1/static-art
```

Reads or updates the resting/static screen bitmap. The payload is exactly 200x200, 1-bit, row encoded. `1` or `#` means black pixel; `0` or `.` means white pixel. No text is composited over this image.

```json
{
  "width": 200,
  "height": 200,
  "encoding": "rows",
  "rows": [
    "0000000000..."
  ]
}
```

The `rows` array must contain 200 strings, each 200 characters long.

The partner CLI can build this payload from `.pbm` raster files directly, or from common raster formats such as `.png` when Pillow is installed.

```http
GET /v1/audio
GET /v1/audio/{audio_id}
DELETE /v1/audio
```

Lists, downloads, or wipes retained WAV recordings from the TF card. List items include `created_at`, `duration_ms`, `synced`, and `transcript_path`; legacy WAVs receive deterministic metadata derived from their filename and header. `DELETE /v1/audio` also removes transcript and note metadata JSON sidecars and returns the number of audio files deleted. It returns `409 Conflict` while recording or playback is active.

```http
PUT /v1/transcripts/{audio_id}
```

Uploads a JSON transcription containing non-empty `text` for an existing recording. A successful upload atomically stores the transcript and marks the note synced; the READ view is populated from these transcript records.

```http
PUT /v1/calendar/today
```

Uploads normalized events for the current local day.

```http
POST /v1/ota
```

Reserved for partner-driven firmware updates after rollback and version checks are implemented.

## USB-C Partner Commands

The firmware also accepts a small line protocol on the USB Serial/JTAG console for maintenance when Wi-Fi is unavailable. The firmware console must be configured with USB Serial/JTAG as the primary console input; a secondary USB log console is output-only for `stdin` consumers.

```text
PJ_STATUS
PJ_WIFI_HEX 4c61622057694669 70617373776f7264 746f6b656e
PJ_TIME 2026 06 20 14 05
PJ_WIPE_RECORDINGS
PJ_AUDIO_TONE [0|1|-] [dout_gpio] [pa=0|1|-] [dout=gpio] [pwr=0|1|-] [gpio44=0x00..0xff] [gp45=0x00..0xff]
PJ_MIC_CHECK [duration_ms] [ms=1..10000] [gain_db=0..42]
```

`PJ_WIFI_HEX` stores hex-encoded UTF-8 `ssid`, `password`, and bearer-token strings in NVS. It intentionally does not echo credentials back over serial.
`PJ_AUDIO_TONE` plays a generated diagnostic tone. Its optional first argument forces the speaker PA GPIO level; `-` keeps the firmware default. Its optional second argument temporarily routes I2S TX data to a DOUT GPIO for board pin-map diagnosis. The named arguments expose the same probes plus temporary audio power GPIO and ES8311 register overrides; the firmware restores the normal route, power level, and register values after the tone.
`PJ_MIC_CHECK` samples the ES8311 microphone path without creating a note file and returns input statistics: `peak`, `avg_abs`, `clipped`, `near_zero`, `read_errors`, and `silent`. The production recording gain is `42 dB`; use lower diagnostic overrides only when measuring input headroom.

Responses start with `PJ_OK` or `PJ_ERR` followed by a compact JSON object. Normal ESP-IDF logs may appear on the same serial stream, so clients should scan for those prefixes.

## Calendar Event Shape

```json
{
  "date": "2026-06-06",
  "events": [
    {
      "source_id": "google-event-id",
      "title": "Design review",
      "start": "2026-06-06T09:00:00-07:00",
      "end": "2026-06-06T09:30:00-07:00",
      "all_day": false,
      "location": "Office",
      "updated": "2026-06-05T18:30:00Z"
    }
  ]
}
```
