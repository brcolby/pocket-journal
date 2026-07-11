# Partner Sync

The partner CLI is the source of truth for laptop-side sync state.

## Commands

```sh
pj provision --ssid <ssid> --password <password>
pj provision --ssid <ssid> --password <password> --serial-port /dev/cu.usbmodem1101
pj discover
pj sync --device <device-id>
pj calendar sync --device <device-id>
pj settings get --device <device-id>
pj settings set --device <device-id> volume=8 theme=dark
pj settings set --device <device-id> alarm_enabled=true alarm_hour=7 alarm_minute=30
pj device sync-time --device <device-id>
pj device tone
pj device tone --pa-level 1
pj device tone --pa-level 0 --dout-gpio 45
pj device tone --audio-power-level 0
pj device tone --codec-gp45 0xff
pj device mic-check --duration-ms 2000
pj device mic-check --duration-ms 2000 --gain-db 30
pj recordings wipe --device <device-id> --yes
pj home get --device <device-id>
pj home set --device <device-id> --file home.json
pj static-art get --device <device-id>
pj static-art set --device <device-id> --file static-art.pbm
```

Wi-Fi commands use the paired device config created by provisioning. For USB-C maintenance when Wi-Fi is unavailable, install the USB extra. The CLI auto-detects `/dev/cu.usbmodem1101` or the single attached USB serial port; pass `--serial-port` only to override detection.

```sh
cd partner
pip install -e '.[usb]'
pj device sync-time
pj recordings wipe --yes
```

`pj provision --serial-port ...` stores Wi-Fi credentials and a generated API bearer token on the device, then writes the paired device profile into the local partner config.

Without `--serial-port`, provisioning discovers a device advertising as `PJ-XXXXXX` and uses the Pocket Journal GATT service. Credentials are written as separate bounded values, then committed asynchronously so the BLE request does not block on NVS or Wi-Fi startup.

| Value | UUID |
| --- | --- |
| Service | `7e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| SSID | `7e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Password | `7e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| API token | `7e400004-b5a3-f393-e0a9-e50e24dcca9e` |
| Commit | `7e400005-b5a3-f393-e0a9-e50e24dcca9e` |
| Status | `7e400006-b5a3-f393-e0a9-e50e24dcca9e` |

The status characteristic returns the device id, provisioning state, and current Wi-Fi state. After credentials are stored, firmware connects as a station, reconnects after disconnects, and advertises `_pocket-journal._tcp.local.` over mDNS.

Only one process can own the USB serial port. Quit `idf.py monitor` with `Ctrl+]` before running the partner utility over USB-C.

## Data Stored Locally

By default the partner stores data under `~/.pocket-journal`:

- `config.json`: paired devices and tokens.
- `audio/<device-id>/`: downloaded WAV files.
- `transcripts/<device-id>/`: transcript JSON files.
- `sync-log.jsonl`: append-only sync results.

Device note metadata is durable under `/sdcard/pj/notes`. Partner sync skips notes already reported as `synced`, so repeated sync runs only transcribe new recordings.

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
