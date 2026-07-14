# Static Art

The resting screen accepts one custom 200x200 monochrome bitmap through the authenticated device API.

## Built-In Artwork

The product default is preserved at
`assets/static/pocket-journal-default.png`. It is a 200x200, 8-bit grayscale,
non-interlaced PNG without alpha. Its pixels are already strictly monochrome
(`0` or `255`), and its SHA-256 is
`ef0fb9a1a2764e19d04056ee57bf9af0c86c2baba2aae098ed945dc07c0d4e9d`.

`tools/generate_static_art.py` validates and decodes the PNG without a third-party
image library. It treats grayscale values below 128 as black and generates:

- `assets/static/pocket-journal-default.pbm`, an ASCII PBM accepted by `pj static-art set`
- `firmware/components/pj_ui/include/pj_default_static_art.h`, dimensions and generation metadata
- `firmware/components/pj_ui/pj_default_static_art.c`, the compiled 5,000-byte bitmap

The compiled bitmap uses the same representation as `pj_static_art_t`: pixels
are row-major from the top-left, black is `1`, and the first pixel in each byte
is bit 0. Regenerate and verify the committed outputs with:

```sh
make generate-static-art
make check-static-art
```

When the device has no valid custom artwork on microSD, firmware and simulator
render this compiled bitmap without text or compositing. A valid custom bitmap
still takes precedence over the built-in image.

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
