# Device API

The ESP32 exposes a versioned LAN API after BLE provisioning and Wi-Fi connection.

All requests use a per-device bearer token created during provisioning:

```http
Authorization: Bearer <pairing-token>
```

TLS is intentionally deferred for v1 LAN derisking.

## Endpoints

```http
GET /v1/status
```

Returns device id, firmware version, board profile, Wi-Fi state, storage state, battery state if available, and pending sync counts.

The time/temp screen can display battery percentage when firmware can read it from the board power-management path. Until hardware bring-up confirms that path, simulator and native tests use a dummy percentage.

```http
GET /v1/settings
PUT /v1/settings
```

Reads or updates the larger partner-managed settings surface.

Expected v1 keys include `theme` with `light` or `dark`, `volume` from `0` to `10`, and sync status fields returned by `/v1/status`.

```http
GET /v1/home
PUT /v1/home
```

Reads or updates the custom home screen design supplied by the partner app. The device should retain up to five slots.

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
```

Lists and downloads unsynced or retained WAV recordings from the TF card.

```http
PUT /v1/transcripts/{audio_id}
```

Uploads a transcription for a recording.

```http
PUT /v1/calendar/today
```

Uploads normalized events for the current local day.

```http
POST /v1/ota
```

Reserved for partner-driven firmware updates after rollback and version checks are implemented.

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
