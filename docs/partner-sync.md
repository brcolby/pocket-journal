# Partner Sync

The partner CLI is the source of truth for laptop-side sync state.

## Commands

```sh
pj provision --ssid <ssid> --password <password>
pj provision --ssid <ssid> --password <password> --serial-port /dev/cu.usbmodem1101
pj provision --ssid <ssid> --password <password> --ble
pj discover
pj transcription setup --model /path/to/ggml-base.en-q5_0.bin
pj transcription status --digest
pj transcription benchmark --manifest /path/to/manifest.json --model /path/to/ggml-base.en-q5_0.bin --output /path/to/report.json
pj sync
pj companion serve
pj library list
pj library tui
pj library serve
pj settings get
pj settings set volume=8 theme=dark
pj settings set alarm_enabled=true alarm_hour=7 alarm_minute=30
pj settings set clock_24h=false temperature_unit=f transcript_font_size=3
pj device status
pj device wifi-diagnostics
pj device usb-recover
pj device sync-time
pj device tone
pj device tone --pa-level 1
pj device tone --pa-level 0 --dout-gpio 45
pj device tone --audio-power-level 0
pj device tone --codec-gp45 0xff
pj device mic-check --duration-ms 2000
pj device mic-check --duration-ms 2000 --gain-db 30
pj recordings wipe --yes
```

`pj discover` reports both attached USB serial ports and devices advertised over
mDNS; it does not open or retain the USB port. Wi-Fi commands use the paired
device config created by provisioning. When one
paired device is configured, or one paired device is visible over mDNS, LAN
commands select it automatically. Pass `--device` when multiple candidates are
available. USB-C support is included in the standard partner CLI install. The CLI
auto-detects `/dev/cu.usbmodem1101` or the single attached USB serial port; pass
`--serial-port` only to override detection.

```sh
cd partner
pip install -e .
pj device sync-time
pj recordings wipe --yes
```

`pj provision` uses USB-C by default. It stores Wi-Fi credentials and a generated
API bearer token on the device, writes the paired device profile into the local
partner config, then sets and validates device time from the host. The result
reports the host UTC anchor, local offset, and device civil time without exposing
credentials. A time-sync failure does not discard the rotated token; retry with
`pj device sync-time`. Pass `--serial-port` only when auto-detection cannot select
the intended device.

Pass `--ble` to provision without a cable. BLE provisioning discovers a device advertising as `PJ-XXXXXX` and uses the Pocket Journal GATT service. The SSID, password, token, and commit characteristics require an encrypted paired BLE connection before firmware accepts writes. The current firmware uses LE Secure Connections with bonding and no-display/no-input pairing; this prevents unauthenticated plaintext credential writes, but a product-owner passkey or display-confirmation UX is still required before claiming MITM-resistant first-time provisioning. Credentials are written as separate bounded values, then committed asynchronously so the BLE request does not block on NVS or Wi-Fi startup.

| Value | UUID |
| --- | --- |
| Service | `7e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| SSID | `7e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Password | `7e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| API token | `7e400004-b5a3-f393-e0a9-e50e24dcca9e` |
| Commit | `7e400005-b5a3-f393-e0a9-e50e24dcca9e` |
| Status | `7e400006-b5a3-f393-e0a9-e50e24dcca9e` |

The status characteristic returns the device id, provisioning state, and current Wi-Fi state. After credentials are stored, firmware connects as a station, reconnects after disconnects, and advertises `_pocket-journal._tcp.local.` over mDNS.

Only one process can own the USB serial port. Start `idf.py monitor` only for an
explicit log session, then quit it with `Ctrl+]` before running the partner utility
over USB-C. Partner commands request exclusive ownership, preset DTR/RTS before
opening, and release the descriptor on success, timeout, error, or interruption.

After closing the monitor, `pj device usb-recover` probes the application protocol
and recognizable ROM output. When reset is needed, it first runs a bounded esptool
USB-JTAG watchdog reset and then uses a short RTS fallback if that fails. It always
reaps the esptool child, closes the descriptor, and returns DTR/RTS to idle. Use
`--probe-only` to inspect without resetting.

If the serial protocol and USB-JTAG are both silent, physically re-enumerate the
board with its dedicated reset control or by unplugging and reconnecting USB-C.
Keep AUX/BOOT released for normal boot; hold it during reset only when intentionally
entering ROM download mode.

`pj device wifi-diagnostics` presents an allowlisted, credential-safe view of
provisioning, connection, DHCP, disconnect, retry, and radio state. Older firmware
only reports a subset; missing values remain `null` or `unknown` rather than being
guessed.

## Note Sync And Library

`pj sync` manually downloads new recordings over USB-C or authenticated LAN/Wi-Fi,
transcribes them locally, writes transcripts back to the matching device record,
and indexes the local result. Its default backend is the CPU-only `whisper.cpp`
CLI. No model or runtime is downloaded implicitly. Configure the pinned artifacts
once and check them before a long sync:

```sh
pj transcription setup --model /models/ggml-base.en-q5_0.bin
pj transcription status --digest
pj sync
```

`pj transcription setup` accepts an existing Q5_0 model only when its size and
SHA-256 match the production baseline. It recognizes the benchmarked Apple
Silicon Homebrew 1.9.1 runtime; other local builds require an explicit expected
runtime digest, source, and license. `--download-runtime` opts in to a pinned
official 1.9.1 archive on supported Linux/Windows platforms. `--download-model`
opts in to the immutable official F16 source and derives Q5_0 with the verified
quantizer. Both paths use bounded staging, digest checks, and atomic replacement;
config remains unchanged after an offline, truncated, wrong-digest, extraction,
or quantization failure. See [Install](install.md) for the complete commands.

To let the device's Settings **Sync** action initiate that pipeline, keep the
companion running. USB-C polling is enabled by default, and the same process also
advertises the mutually authenticated LAN service. It runs each request
asynchronously and reports conditional generation progress back to firmware:

```sh
pj companion serve
```

The companion resolves persisted verified setup when no command-line or
environment override is present and refuses to start if either artifact changed
after setup. Its transcription path does not need network access.

See [Device-Initiated Sync](device-initiated-sync.md) for discovery,
authentication, retry, and port-selection details. Explicit `pj sync --transport
usb` remains available and does not require the listener.

For device-initiated LAN sync, the raw pairing token is not sent in control
requests. Domain-separated request and response HMACs bind the direct device IPv4
peer, exact operation identity, and a fresh per-request challenge. Durable
companion replay state resumes an unfinished operation after restart and withholds
terminal results until they are on disk. A separately derived temporary
credential can only list/read audio and upload a transcript while that
LAN claim is active. The transport is still plain HTTP: it authenticates participants and
limits authorization, but it does not encrypt metadata or payloads and does not
protect data-plane content from an active on-path attacker. Prefer USB-C outside a
trusted local network.

The listener opens USB-C only for bounded commands and releases the descriptor
between its default two-second polls. Use `--serial-port`,
`--usb-poll-interval`, or diagnostic `--no-usb` as needed. Stop any serial monitor
while USB servicing is enabled.

The recommended baseline is `ggml-base.en-q5_0.bin`. It is small enough for the
16 GB, CPU-only product profile, passed the repository benchmark on macOS, and
keeps runtime packaging independent of PyTorch. See
[Transcription Benchmark](transcription-benchmark.md) for the measured F16/Q5_0
comparison, corpus format, and Linux/Windows reproduction procedure.
`small.en` can be evaluated when its additional latency is acceptable.
The partner refuses models above its 2 GiB guardrail for this backend. The legacy
`--backend hf` path remains available for comparison, and `--backend fake`
creates local-only test transcripts that are never uploaded.

The bounded USB firmware protocol also supports the complete sync data path.
`PJ_AUDIO_LIST` returns one hex-safe item per snapshot-paged response,
`PJ_AUDIO_READ` returns at most 256 bytes per offset-checked response, and
`PJ_TRANSCRIPT_BEGIN/WRITE/COMMIT` streams 192-byte write chunks and stages at
most 64 KiB before a digest-verified
atomic commit. Failed uploads receive a best-effort `PJ_TRANSCRIPT_ABORT`.
`pj sync --transport usb` selects it explicitly; `auto` prefers an unambiguous
USB-C device and falls back to paired LAN discovery when none is attached.

Every synced note has a stable identity derived from `(device_id, audio_id)` and
pairs one editable title with its local WAV and transcript. All interfaces use
the same structured library:

```sh
pj library list [--search TEXT]
pj library show NOTE_ID
pj library title NOTE_ID 'New title'
pj library tui
pj library serve                 # http://127.0.0.1:8766
```

The web UI binds only to a loopback address, rejects non-loopback `Host` headers
to prevent DNS rebinding, sets restrictive browser security headers, requires an
unguessable per-process form token for title changes, and
serves audio only by validated library identity. It supports byte ranges for
normal browser seeking. The terminal UI uses numbered page commands and opens
audio through the operating system without constructing a shell command.

## Data Stored Locally

By default the partner stores data under `~/.pocket-journal`:

- `config.json`: paired devices/tokens plus non-secret verified transcription
  paths, hashes, provenance, licenses, version, and CPU thread count.
- `transcription/`: explicitly acquired runtime/model artifacts and transient
  same-filesystem staging directories; failed staging is removed before return.
- `audio/<device-id>/`: downloaded WAV files.
- `transcripts/<device-id>/`: transcript JSON files.
- `jobs/<device-id>/`: resumable, versioned per-note sync state.
- `companion/<device-id>.json`: device-initiated generation high-water and exact
  terminal replay state for the current pairing epoch.
- `library.sqlite3`: migrated structured index pairing title, WAV, transcript,
  stable device identity, source digest, and sync status.
- `sync-log.jsonl`: append-only sync results.

Existing job/audio/transcript files are imported into the database
idempotently. User-edited titles are retained on later syncs. Audio remains in
the existing file tree; the database stores only paths constrained beneath the
partner data directory.

Device note metadata is durable under `/sdcard/pj/notes`. Partner sync skips notes already reported as `synced`, so repeated sync runs only transcribe new recordings.

For sandboxed or test runs, pass `--data-dir <path>` to commands that need the paired-device config.

## Transcription Alternatives

The production baseline is `whisper.cpp` with local `base.en` weights and an
explicit CPU fallback on macOS, Linux, and Windows. The prior
`distil-whisper/distil-large-v3.5` Transformers backend remains a quality
comparator; it is not the default because its larger model and PyTorch package
are a poorer fit for a simple <=16 GB CPU installation. Model acquisition is a
separate deliberate operator step, while transcription and sync operate locally
without a cloud service.
