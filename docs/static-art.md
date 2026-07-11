# Static Art

The resting screen accepts one custom 200x200 monochrome bitmap through the authenticated device API.

## API

`PUT /v1/static-art` accepts JSON with these exact bitmap dimensions and row encoding. The shape below shows one row; a request contains 200 such rows.

```json
{
  "width": 200,
  "height": 200,
  "encoding": "rows",
  "rows": [
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
  ]
}
```

The `rows` array must contain exactly 200 strings of exactly 200 characters each. `1` and `#` are black pixels; `0` and `.` are white pixels. Other dimensions, encodings, row counts, row lengths, and pixel characters return `400 Bad Request` with an `error` string. Bodies larger than 65,536 bytes return `413 Payload Too Large`.

A successful update returns:

```json
{"updated":true}
```

`GET /v1/static-art` returns the stored bitmap in the same shape, normalized to `0` and `1`. It returns `404 Not Found` with `{"error":"static art not set"}` until the first successful update. Both methods require the same bearer token as the other `/v1` routes.

## Persistence And Rendering

The device packs the bitmap into 5,000 bytes and stores versioned, CRC-protected records in two slots on the required microSD card. An update writes and syncs the inactive slot, reads it back for validation, and then atomically commits a small active-slot pointer in NVS. The in-memory image and display-update signal change only after that commit succeeds, so an interrupted or failed update continues using the previous valid slot. On boot, records with an invalid version, size, or checksum are ignored and the other slot is tried as a fallback.

The static screen renders the stored bitmap without text or other compositing. If no valid custom bitmap exists, it renders the built-in resting image. The custom bitmap remains active across sleep/wake and reboot lifecycles.
