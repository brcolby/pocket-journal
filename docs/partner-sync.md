# Partner Sync

The partner CLI is the source of truth for laptop-side sync state.

## Commands

```sh
pj provision --ssid <ssid> --password <password>
pj discover
pj sync --device <device-id>
pj calendar sync --device <device-id>
pj settings get --device <device-id>
pj settings set --device <device-id> volume=5
pj home get --device <device-id>
pj home set --device <device-id> --file home.json
pj static-art get --device <device-id>
pj static-art set --device <device-id> --file static-art.pbm
```

## Data Stored Locally

By default the partner stores data under `~/.pocket-journal`:

- `config.json`: paired devices and tokens.
- `audio/<device-id>/`: downloaded WAV files.
- `transcripts/<device-id>/`: transcript JSON files.
- `sync-log.jsonl`: append-only sync results.

For sandboxed or test runs, pass `--data-dir <path>` to commands that need the paired-device config.

## Home Design JSON

The partner can push up to five home slots:

```json
{
  "title": "Pocket Journal",
  "slots": [
    {"label": "Notes", "icon": "stylus_note", "state": "notes"},
    {"label": "Time", "icon": "schedule", "state": "time"},
    {"label": "Settings", "icon": "settings", "state": "settings"},
    {"label": "Calendar", "icon": "calendar_month", "state": "calendar"},
    {"label": "Sync", "icon": "sync", "state": "sync"}
  ]
}
```

Icons use Google Material Symbols names. Firmware should eventually include generated 1-bit glyphs for the selected icon subset.

## Static Art

The resting screen accepts raster art through `pj static-art set`.

Supported inputs:

- `.pbm`: ASCII PBM `P1`, exactly 200x200, no extra dependencies.
- `.json`: row-encoded device payload.
- Other raster formats such as `.png` or `.jpg` when Pillow is installed.

Generated default asset:

```text
assets/static/smiley.pbm
```

Device payload shape:

```json
{
  "width": 200,
  "height": 200,
  "encoding": "rows",
  "rows": [
    "........................................................................................................................................................................................................"
  ]
}
```

Use 200 rows of 200 pixels. `#`/`1` are black pixels, `.`/`0` are white pixels. The device does not overlay date, time, or navigation text on this screen.

## Transcription

The target model id is `distil-whisper/distil-large-v3.5`.

The code uses a backend abstraction so tests can run with a fake backend and production can use Hugging Face Transformers or a later Apple Silicon optimized backend.
