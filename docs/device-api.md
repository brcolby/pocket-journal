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

```http
GET /v1/settings
PUT /v1/settings
```

Reads or updates the larger partner-managed settings surface.

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

