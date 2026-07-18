# Partner CLI Reference

`pj` is the laptop-side command for provisioning, device maintenance, note
sync, transcription, local-library browsing, and signed OTA updates. This page
documents public release `0.0`, whose Python package version is `0.0.0`.

For installation and the first connection, start with [Install and
Flashing](install.md). For sync behavior and the on-disk data model, see
[Partner Sync](partner-sync.md).

## Help, output, and configuration

Every command has local help:

```sh
pj --help
pj device --help
pj device status --help
```

Commands intended for scripting print JSON to standard output. Progress and
server addresses are written to standard error. Argument errors normally exit
with status 2; an unavailable device, failed operation, or unavailable
transcription backend exits nonzero.

Partner state defaults to `~/.pocket-journal`. Set `POCKET_JOURNAL_HOME` before
running `pj`, or pass `--data-dir DIR` to commands that expose that option, to
use another location. The directory contains bearer tokens and should remain
private to the local user.

There is no `pj --version` flag. For a virtual-environment install, inspect the
installed package metadata with:

```sh
python -c 'from importlib.metadata import version; print(version("pocket-journal-partner"))'
```

For pipx, use:

```sh
pipx runpip pocket-journal-partner show pocket-journal-partner
```

## Device and transport selection

The following options recur on commands that can use USB-C or LAN:

| Option | Meaning |
| --- | --- |
| `--transport auto\|usb\|lan` | `auto` prefers the one unambiguous USB-C device and otherwise uses a paired LAN device. |
| `--serial-port PORT` | Override USB auto-detection, for example `/dev/cu.usbmodem1101`, `/dev/ttyACM0`, or `COM5`. |
| `--serial-baud N` | USB serial rate; default `115200`. |
| `--timeout SECONDS` | Per-operation timeout. The default is shown in the relevant section below. |
| `--device ID` | Select one paired device when more than one is configured or reachable. |
| `--base-url URL` | Override LAN discovery with a URL such as `http://192.168.1.42`. |
| `--token TOKEN` | Override the bearer token saved during provisioning. Avoid putting tokens in shell history. |
| `--data-dir DIR` | Use a partner data directory other than `~/.pocket-journal`. |

Only one process can own a USB serial port. Exit `idf.py monitor` with
`Ctrl+]`, and stop another running `pj companion serve`, before issuing a
one-shot USB command.

## `pj provision`

Store Wi-Fi credentials and a generated API token on the device, save the
paired profile locally, and set device time from the host when using USB-C.
USB-C is the default transport.

```text
pj provision --ssid SSID --password PASSWORD
             [--serial-port PORT | --ble] [--ble-name NAME]
             [--base-url URL] [--data-dir DIR]
             [--serial-baud N] [--timeout SECONDS] [--mock]
```

- `--ssid` and `--password` are required. Quote values that contain shell
  metacharacters.
- `--serial-port` overrides USB auto-detection.
- `--ble` uses BLE instead of USB and requires the `ble` optional dependency.
- `--ble-name` selects a specific `PJ-XXXXXX` advertisement and requires
  `--ble`.
- `--base-url` saves a known LAN URL in the paired profile.
- `--serial-baud` defaults to `115200`; `--timeout` defaults to 6 seconds.
- `--mock` is a development-only simulated BLE path and requires `--ble`.

Examples:

```sh
pj provision --ssid 'Home Wi-Fi' --password 'correct horse battery staple'
pj provision --ssid 'Home Wi-Fi' --password 'secret' --serial-port /dev/ttyACM0
pj provision --ssid 'Home Wi-Fi' --password 'secret' --ble --ble-name PJ-A1B2C3
```

BLE provisioning cannot set the real-time clock. Connect over USB-C or LAN and
run `pj device sync-time` afterward.

## `pj discover`

List USB serial ports and Pocket Journal devices advertised over mDNS. The
command does not retain a serial port.

```sh
pj discover
```

## `pj device`

### Status and time

```text
pj device status [connection options]
pj device wifi-diagnostics [connection options]
pj device sync-time [connection options]
```

All three accept `--device`, `--base-url`, `--token`, `--data-dir`,
`--serial-port`, `--serial-baud`, `--timeout`, and
`--transport auto|usb|lan`. Their timeout defaults to 6 seconds.

- `status` returns a credential-safe device/capability summary, including
  `firmware_version`.
- `wifi-diagnostics` explains provisioning, association, DHCP, retry, and radio
  state without printing credentials.
- `sync-time` writes the host's civil time and UTC offset, then validates the
  device response.

```sh
pj device status --transport usb
pj device wifi-diagnostics --transport lan --device pj-a1b2c3
pj device sync-time
```

### Stop an interval

Silence and durably reset a running interval over USB-C:

```text
pj device stop-interval [--serial-port PORT] [--serial-baud N]
                        [--timeout SECONDS]
```

The baud rate defaults to `115200` and the timeout to 6 seconds.

### USB recovery

Probe the application protocol and ROM output, and when necessary attempt a
bounded ESP32-S3 USB-JTAG watchdog reset followed by an RTS fallback:

```text
pj device usb-recover [--serial-port PORT] [--serial-baud N]
                      [--timeout SECONDS] [--probe-only]
```

The timeout defaults to 8 seconds. `--probe-only` reports state without
resetting. Keep AUX/BOOT released for normal boot.

### Audio diagnostics

These are USB-only hardware diagnostics:

```text
pj device tone [--device LABEL] [--serial-port PORT] [--serial-baud N]
               [--timeout SECONDS] [--pa-level 0|1] [--dout-gpio GPIO]
               [--audio-power-level 0|1] [--codec-gpio44 VALUE]
               [--codec-gp45 VALUE]

pj device mic-check [--device LABEL] [--serial-port PORT] [--serial-baud N]
                    [--timeout SECONDS] [--duration-ms N] [--gain-db DB]
```

`tone` defaults to a 6-second timeout. Register overrides accept decimal or
`0x`-prefixed values. `mic-check` defaults to 1,500 ms of sampling and an
8-second timeout; `--gain-db` accepts 0 through 42. The `--device` value on
these commands is only an output label, not a transport selector.

## `pj firmware`

These commands use an already paired LAN/Wi-Fi connection; they do not update
firmware over USB-C.

```text
pj firmware status [--device ID] [--base-url URL] [--token TOKEN]
                   [--data-dir DIR]

pj firmware update --file APP.bin [--manifest FILE] [--signature FILE]
                   [--device ID] [--base-url URL] [--token TOKEN]
                   [--data-dir DIR] [--yes]
                   [--reconnect-timeout SECONDS]
```

- `status` reports the running image, OTA slots, trust readiness, and last boot
  outcome.
- `update` requires an app-only ESP image, not the merged factory image. By
  default it reads `APP.bin.manifest.json` and `APP.bin.sig` beside the image.
- `--manifest` and `--signature` select explicitly named sidecars.
- Without `--yes`, an interactive terminal must confirm activation. A
  non-interactive invocation must pass `--yes`.
- `--reconnect-timeout` defaults to 90 seconds.

Release `0.0` is an ordinary unsigned factory/USB release and is not an OTA
candidate. A future OTA bundle must be built with both configured trust paths,
must carry an increasing strict `X.Y.Z` version, and must include a canonical
manifest plus P-256 DER signature. A trust-configured factory build at `0.0.0`
may use `0.0.1` as its first signed successor. See [Signed OTA
builds](install.md#signed-ota-builds).

## `pj sync`

Download recordings, transcribe locally, upload transcripts when the selected
backend permits it, and update the local library.

```text
pj sync [connection options] [--transport auto|usb|lan]
        [--backend whisper-cpp|hf|fake] [--model FILE]
        [--whisper-executable FILE] [--threads N] [--reprocess]
```

This command accepts `--device`, `--base-url`, `--token`, `--data-dir`,
`--serial-port`, `--serial-baud`, and `--timeout`. Its timeout defaults to 20
seconds.
`whisper-cpp` is the default backend. `--model`, `--whisper-executable`, and
`--threads` override persisted transcription setup for this run.
The transport defaults to `auto`. `--reprocess` includes notes already marked
synced. `--backend fake` creates local test transcripts and deliberately does
not upload them.

```sh
pj sync
pj sync --transport usb --serial-port /dev/cu.usbmodem1101
pj sync --transport lan --device pj-a1b2c3
```

## `pj companion serve`

Run the long-lived service used when the device initiates a sync:

```text
pj companion serve [--device ID] [--data-dir DIR]
                   [--host ADDRESS] [--port N]
                   [--serial-port PORT] [--usb-poll-interval SECONDS]
                   [--no-usb] [--advertise-address IPV4]... [--no-mdns]
                   [--backend whisper-cpp|hf] [--model FILE]
                   [--whisper-executable FILE] [--threads N]
```

- The LAN listener defaults to `0.0.0.0:8765`; mDNS advertisement and bounded
  USB polling are enabled by default.
- `--usb-poll-interval` defaults to 2 seconds and accepts 0.25 through 60.
- Repeat `--advertise-address` for multiple IPv4 interfaces.
- `--no-usb` and `--no-mdns` are diagnostic switches.
- `--device` is required when the config contains multiple paired devices.
- The transcription backend defaults to `whisper-cpp`.
- The LAN protocol authenticates requests but does not encrypt payloads. Use it
  only on a trusted network; prefer USB-C elsewhere.

## `pj library`

Browse and edit the local note library:

```text
pj library list [--limit N] [--offset N] [--search TEXT] [--data-dir DIR]
pj library show NOTE_ID [--data-dir DIR]
pj library title NOTE_ID TITLE [--data-dir DIR]
pj library tui [--data-dir DIR]
pj library serve [--data-dir DIR] [--host ADDRESS] [--port N] [--no-open]
```

`list` defaults to 100 rows at offset 0. `serve` defaults to
`127.0.0.1:8766`; `--no-open` suppresses automatic browser launch. Keep the
library server loopback-only unless you have independently secured it.

Examples:

```sh
pj library list --search interview
pj library show NOTE_ID
pj library title NOTE_ID 'Project interview'
pj library tui
pj library serve --no-open
```

## `pj transcription`

### Inspect or configure the default backend

```text
pj transcription status [--model FILE] [--whisper-executable FILE]
                        [--threads N] [--digest] [--data-dir DIR]

pj transcription setup [--runtime FILE] [--runtime-sha256 HEX]
                       [--runtime-source TEXT] [--runtime-license ID]
                       [--model FILE] [--download-runtime]
                       [--download-model] [--threads N]
                       [--download-timeout SECONDS]
                       [--process-timeout SECONDS] [--data-dir DIR]
```

`status --digest` recomputes hashes. Command-line paths override the
`PJ_WHISPER_CPP` and `PJ_WHISPER_MODEL` environment variables, which in turn
override persisted setup for one invocation.

`setup` verifies and persists exactly whisper.cpp 1.9.1 and the pinned
`ggml-base.en-q5_0.bin` model. For an operator-supplied runtime not already
recognized, provide its expected SHA-256, source, and license. Network access
occurs only when `--download-runtime` or `--download-model` is present. Download
and process timeouts each default to 900 seconds.

```sh
pj transcription setup \
  --runtime /usr/local/bin/whisper-cli \
  --runtime-sha256 SHA256 \
  --runtime-source SOURCE \
  --runtime-license MIT \
  --model /models/ggml-base.en-q5_0.bin
pj transcription status --digest
```

### Benchmark

```text
pj transcription benchmark --manifest FILE [--model FILE]
                           [--whisper-executable FILE] [--threads N]
                           [--runs N] [--timeout SECONDS] [--output FILE]
                           [--runtime-root DIR] [--runtime-source TEXT]
                           [--runtime-license ID] [--model-source TEXT]
                           [--model-license ID]
```

The benchmark defaults to two runs and a 3,600-second timeout per process. See
[Transcription Benchmark](transcription-benchmark.md) for the manifest schema
and acceptance thresholds.

## `pj settings`

Read or atomically update device settings:

```text
pj settings get [connection options]
pj settings set KEY=VALUE [KEY=VALUE ...] [connection options]
```

Both commands accept `--device`, `--base-url`, `--token`, `--data-dir`,
`--serial-port`, `--serial-baud`, `--timeout`, and
`--transport auto|usb|lan`. The timeout defaults to 6 seconds.

| Key | Accepted value |
| --- | --- |
| `volume` | integer `0` through `10` |
| `theme` | `light` or `dark` |
| `alarm_enabled` | `true` or `false` |
| `alarm_hour` | integer `0` through `23` |
| `alarm_minute` | integer `0` through `59` |
| `timer_seconds` | integer `30` through `86400` |
| `interval_seconds` | integer `30` through `86400` |
| `clock_24h` | `true` or `false` |
| `temperature_unit` | `c` or `f` |
| `transcript_font_size` | `2` or `3` |

```sh
pj settings get
pj settings set volume=8 theme=dark
pj settings set alarm_enabled=true alarm_hour=7 alarm_minute=30
pj settings set clock_24h=false temperature_unit=f transcript_font_size=3
```

## `pj recordings`

List, download, or erase recordings on the device:

```text
pj recordings list [connection options]
pj recordings download --audio-id ID [--output-dir DIR] [connection options]
pj recordings wipe --yes [connection options]
```

All three accept `--device`, `--base-url`, `--token`, `--data-dir`,
`--serial-port`, `--serial-baud`, `--timeout`, and
`--transport auto|usb|lan`. `list` and `download` default to a 20-second
timeout; `wipe` defaults to 6 seconds. Downloads go to the current directory
unless `--output-dir` is supplied.

`wipe` is destructive and refuses to run without `--yes`. It removes device
recordings; it does not delete the partner's already downloaded local library.

```sh
pj recordings list
pj recordings download --audio-id ID_FROM_LIST --output-dir ./recordings
pj recordings wipe --yes
```
