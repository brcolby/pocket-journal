# Pocket Journal

Pocket Journal is open-source firmware and a local-first laptop partner for the
Waveshare ESP32-S3 Touch e-Paper 1.54 V2. The 200 x 200 e-paper device records
notes to a FAT32 TF card; the `pj` partner application provisions Wi-Fi,
transfers recordings over USB-C or a trusted LAN, transcribes them locally, and
provides terminal and browser libraries.

## Supported hardware

- Waveshare SKU `34211`, ESP32-S3-Touch-ePaper-1.54 V2
- 8 MB flash and 8 MB octal PSRAM board variant
- FAT32 TF/microSD card for recordings and durable note data
- USB-C data cable for the initial factory flash and recommended provisioning
- optional 2.4 GHz Wi-Fi or encrypted BLE provisioning

The firmware integrates the e-paper display, touch, RTC, environmental sensor,
SD storage, ES8311 microphone/speaker audio, USB Serial/JTAG partner protocol,
BLE provisioning, Wi-Fi, and authenticated local-network services.

## Install release `0.0`

Download the merged firmware image, partner wheel, and `SHA256SUMS` from the
[GitHub `0.0` release](https://github.com/brcolby/pocket-journal/releases/tag/0.0).
Public release names use `0.0`; embedded firmware and Python package metadata use
`0.0.0`.

Install the partner application with Python 3.11 or newer:

```sh
pipx install ./pocket_journal_partner-0.0.0-py3-none-any.whl
pj --help
```

Connect the board with AUX/BOOT released, find its port, then flash the single
factory image at `0x0`:

```sh
python3 -m venv .flash-venv
. .flash-venv/bin/activate
python -m pip install 'esptool>=5.3,<6'
python -m serial.tools.list_ports -v
python -m esptool --chip esp32s3 --port PORT write-flash 0x0 pocket-journal-firmware-0.0.bin
```

After rebooting with AUX/BOOT released, provision and verify the device:

```sh
pj discover
pj provision --ssid 'YOUR_WIFI_NAME' --password 'YOUR_WIFI_PASSWORD'
pj device status
pj device wifi-diagnostics
```

The merged image is a factory flash that clears internal settings and pairing
state. It does not erase the removable TF card. See [Install and
Flashing](docs/install.md) for checksum verification, pipx and virtual-environment
installs, macOS/Linux/Windows port discovery, ESP-IDF source builds, erase,
flash, and monitor commands, first-run setup, and recovery steps.

## Use the partner

On supported Linux and Windows hosts, the partner can acquire both pinned
transcription artifacts explicitly:

```sh
pj transcription setup --download-runtime --download-model
pj transcription status --digest
pj sync
pj library tui
```

On macOS, the pinned upstream release does not provide a downloadable CLI
archive; install whisper.cpp 1.9.1 locally, then run `pj transcription setup
--download-model`, followed by the same status/sync/library commands. The
partner never downloads a model during `pj sync`.

Useful entry points include:

```sh
pj discover
pj device status
pj settings get
pj recordings list
pj companion serve
pj library serve
```

See the complete [Partner CLI Reference](docs/cli-reference.md) and the detailed
[Partner Sync](docs/partner-sync.md) design. The local library web UI binds to
`127.0.0.1:8766` by default. LAN sync authenticates participants but does not
encrypt payloads, so use it only on a trusted network and prefer USB-C elsewhere.

## Build and test from source

Firmware is pinned to ESP-IDF 6.0.1:

```sh
cd firmware
idf.py set-target esp32s3
idf.py -D PROJECT_VER=0.0.0 build
idf.py -p PORT flash
```

Run the main local checks from the repository root:

```sh
make test
```

Or run focused checks:

```sh
make test-ui
make test-partner
make test-simulator
make simulator
```

The simulator is served at `http://127.0.0.1:8765`. See [Testing](docs/testing.md)
and [Simulator](docs/simulator.md) for tool-specific prerequisites.

## Repository layout

- `firmware/`: ESP-IDF firmware, board services, and shared UI/state-machine core
- `partner/`: installable Python `pj` application
- `simulator/`: browser-based firmware UI simulator
- `tests/`: native firmware-core, partner, simulator, and asset checks
- `tools/`: deterministic asset and geometry generators
- `docs/`: operator, architecture, protocol, hardware, and validation guides

Start with the [documentation index](docs/README.md).

## Release security note

Release `0.0` is an ordinary unsigned, fail-closed factory/USB image and is not
a `pj firmware update` OTA candidate. Future signed OTA images require both the
configured ESP-IDF app-signing key and Pocket Journal P-256 manifest trust, plus
an increasing strict `X.Y.Z` version. See [Signed OTA
builds](docs/install.md#signed-ota-builds).

## License

Pocket Journal is distributed under the [MIT License](LICENSE). Bundled and
generated dependencies are summarized in [Third-Party Notices](THIRD_PARTY_NOTICES.md).
