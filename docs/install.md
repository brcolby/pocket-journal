# Install and Flashing

This guide covers the supported Waveshare ESP32-S3 Touch e-Paper 1.54 V2 board,
public release `0.0`, source firmware builds, and the `pj` partner application.
The public tag and asset names use `0.0`; the firmware descriptor and Python
package metadata use `0.0.0` so subsequent signed firmware can use normal
semantic-version ordering.

## Before you start

You need:

- a Waveshare SKU `34211`, ESP32-S3-Touch-ePaper-1.54 V2;
- a USB-C **data** cable, not a charge-only cable;
- Python 3.11 or newer for the partner application;
- a FAT32 TF/microSD card for recordings and durable device note data; and
- 2.4 GHz Wi-Fi credentials if you want LAN sync or future OTA updates.

Insert or remove the TF card while the board is powered off. Flashing internal
ESP32-S3 storage does not format the card.

For the shortest path, install the partner wheel and flash the merged firmware
image from the [GitHub `0.0` release](https://github.com/brcolby/pocket-journal/releases/tag/0.0).
Developers who need to change firmware should use the [source build](#build-from-source).

## Release files

Release `0.0` contains:

| File | Purpose |
| --- | --- |
| `pocket-journal-firmware-0.0.bin` | One merged ESP32-S3 factory image. Flash it at address `0x0`. It is not an OTA app image. |
| `pocket-journal-firmware-0.0.zip` | Firmware image, flashing guide, SPDX inventory, and applicable license notices in one download. |
| `pocket-journal-firmware-0.0.spdx` | Machine-readable inventory generated from the linked ESP-IDF build. |
| `pocket_journal_partner-0.0.0-py3-none-any.whl` | Recommended installable partner package. |
| `pocket_journal_partner-0.0.0.tar.gz` | Partner source distribution. |
| `THIRD_PARTY_NOTICES.md` | License summary for bundled assets and linked firmware components. |
| `SHA256SUMS` | SHA-256 digests for the published assets. |

Verify the firmware digest after downloading it. On macOS:

```sh
shasum -a 256 pocket-journal-firmware-0.0.bin
```

On Linux:

```sh
sha256sum pocket-journal-firmware-0.0.bin
```

On PowerShell:

```powershell
Get-FileHash .\pocket-journal-firmware-0.0.bin -Algorithm SHA256
```

Compare the printed digest with the matching line in `SHA256SUMS` before
flashing or installing an asset.

## Install the partner application

The base install includes USB-C, LAN discovery, the local library, and
`esptool`. BLE provisioning and the legacy Hugging Face transcription backend
are optional. The default CPU-only whisper.cpp workflow does **not** need the
large Python `transcription` extra.

### Option A: pipx

[`pipx`](https://pipx.pypa.io/stable/installation/) installs `pj` in an isolated
environment while keeping the command available in normal shells:

```sh
pipx install ./pocket_journal_partner-0.0.0-py3-none-any.whl
pj --help
```

If pipx selects an older Python, choose one explicitly:

```sh
pipx install --python python3.11 ./pocket_journal_partner-0.0.0-py3-none-any.whl
```

Add BLE support to that environment only if needed:

```sh
pipx inject pocket-journal-partner 'bleak>=0.22'
```

For an editable install directly from a clone:

```sh
pipx install --editable './partner[ble]'
```

If `pj` is not found after installation, run `pipx ensurepath`, open a new
terminal, and retry.

### Option B: virtual environment

On macOS or Linux, from the repository root or a directory containing the
downloaded wheel:

```sh
python3 -m venv .pj-venv
. .pj-venv/bin/activate
python -m pip install ./pocket_journal_partner-0.0.0-py3-none-any.whl
pj --help
```

On Windows PowerShell:

```powershell
py -3.11 -m venv .pj-venv
.\.pj-venv\Scripts\Activate.ps1
python -m pip install .\pocket_journal_partner-0.0.0-py3-none-any.whl
pj --help
```

For an editable developer install with BLE support:

```sh
python3 -m venv partner/.venv
. partner/.venv/bin/activate
python -m pip install -e './partner[ble]'
```

Use `.[transcription]` or `.[ble,transcription]` only to install the legacy
Transformers/PyTorch comparison backend. Source-tree execution without an
install is also supported:

```sh
cd partner
PYTHONPATH=src python -m pocket_journal_partner --help
```

The CLI currently relies on `pj ... --help` for command discovery and does not
ship generated shell-completion scripts. See the complete [CLI
reference](cli-reference.md).

## Flash the `0.0` release image

The partner wheel depends on a compatible `esptool` (5.3 or newer, below 6).
When `pj` is installed in an active virtual environment, the command is already
available there. A pipx environment is intentionally isolated from the shell's
`python`, so pipx users should create a small flashing environment:

```sh
python3 -m venv .flash-venv
. .flash-venv/bin/activate
python -m pip install 'esptool>=5.3,<6'
```

On Windows PowerShell:

```powershell
py -3.11 -m venv .flash-venv
.\.flash-venv\Scripts\Activate.ps1
python -m pip install 'esptool>=5.3,<6'
```

### 1. Find the serial port

Connect the board with AUX/BOOT released, then run:

```sh
python -m serial.tools.list_ports -v
```

Typical ports are `/dev/cu.usbmodem1101` on macOS, `/dev/ttyACM0` on Linux,
and `COM5` on Windows. `pj discover` reports the same USB candidates once the
partner application is installed.

On Linux, a permission error commonly means the user is not in the serial-port
group. The usual fix is:

```sh
sudo usermod -a -G dialout "$USER"
```

Log out and back in before retrying. Some distributions use a different group;
check the ownership of the actual `/dev/ttyACM*` device.

### 2. Write the merged image

Replace `PORT` with the discovered port. Keep AUX/BOOT released unless you are
deliberately entering ROM download mode.

```sh
python -m esptool --chip esp32s3 --port PORT write-flash 0x0 pocket-journal-firmware-0.0.bin
```

For example:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 write-flash 0x0 pocket-journal-firmware-0.0.bin
```

On PowerShell:

```powershell
python -m esptool --chip esp32s3 --port COM5 write-flash 0x0 .\pocket-journal-firmware-0.0.bin
```

The merged file is a **factory image**. Writing it at `0x0` replaces the
bootloader, partition table, OTA metadata, application, and internal NVS state,
including saved Wi-Fi credentials, API tokens, and settings. It does not erase
the removable TF card.

If automatic reset cannot enter the ROM loader, hold AUX/BOOT, reset or
power-cycle the board, start the command, then release AUX/BOOT. After the write,
reset or power-cycle with AUX/BOOT released for normal boot.

## First-run provisioning

1. Insert the FAT32 TF card with power off, reconnect power, and let the first
   boot complete.
2. Confirm the device is visible with `pj discover`.
3. Provision over USB-C. The command generates and stores the API token and also
   sets and validates local civil time:

   ```sh
   pj provision --ssid 'YOUR_WIFI_NAME' --password 'YOUR_WIFI_PASSWORD'
   ```

   Pass `--serial-port PORT` if more than one serial device is attached.
4. Confirm device, storage, firmware, and Wi-Fi state:

   ```sh
   pj device status
   pj device wifi-diagnostics
   ```

5. If provisioning reports only a time-sync failure, do not provision again;
   retry the independent operation:

   ```sh
   pj device sync-time
   ```

On an unprovisioned device, BLE advertises as `PJ-XXXXXX`. Install the `ble`
extra, then use this cable-free alternative:

```sh
pj provision --ssid 'YOUR_WIFI_NAME' --password 'YOUR_WIFI_PASSWORD' --ble
```

BLE provisioning saves network credentials but cannot set the clock, so follow
it with `pj device sync-time` over USB-C or LAN. The initial no-input/no-display
pairing protects writes with an encrypted bonded link but does not yet provide
MITM-resistant passkey or display confirmation.

## Build from source

### Install ESP-IDF 6.0.1

Firmware is pinned to ESP-IDF 6.0.1 in `firmware/dependencies.lock`. Other
versions are not release-supported. Follow Espressif's [ESP-IDF installation
guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html),
or install a dedicated checkout on macOS/Linux:

```sh
mkdir -p ~/esp
cd ~/esp
git clone --recursive --branch v6.0.1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
idf.py --version
```

On Windows, run `install.bat esp32s3` and then `export.bat` from the ESP-IDF
Command Prompt. The Espressif installer is also supported. Activate the 6.0.1
environment in every new terminal before using `idf.py`.

### Configure and build

From the repository:

```sh
cd firmware
idf.py set-target esp32s3
idf.py -D PROJECT_VER=0.0.0 build
```

The first build may download the exact managed components recorded in
`dependencies.lock`. The project selects the ESP32-S3 target, 8 MB flash, 8 MB
octal PSRAM, USB Serial/JTAG console, and the custom OTA partition table through
`sdkconfig.defaults` and `partitions.csv`.

For a fresh unsigned release-style build, explicitly direct ESP-IDF to a new
temporary sdkconfig. This prevents a checked-in or previously generated
`sdkconfig` from silently carrying local test or signing settings:

```sh
rm -f /tmp/pocket-journal-0.0-sdkconfig
idf.py -B build-release \
  -D SDKCONFIG=/tmp/pocket-journal-0.0-sdkconfig \
  -D PROJECT_VER=0.0.0 build
```

The temporary sdkconfig path must be new or nonexistent before the build. Check
the configuration summary for `/tmp/pocket-journal-0.0-sdkconfig` and the build
output for `App version: 0.0.0` before using its artifacts.

To discard the isolated build and regenerate it:

```sh
idf.py -B build-release fullclean
rm -f /tmp/pocket-journal-0.0-sdkconfig
idf.py -B build-release \
  -D SDKCONFIG=/tmp/pocket-journal-0.0-sdkconfig \
  -D PROJECT_VER=0.0.0 build
```

Do not publish a release build without an explicit `PROJECT_VER`. The running
version is embedded in the ESP application descriptor and is reported by
`pj device status` as `firmware_version`.

### Source-build artifacts

With the default `build` directory, ESP-IDF produces:

| Offset | File | Use |
| --- | --- | --- |
| `0x0` | `build/bootloader/bootloader.bin` | ESP32-S3 bootloader. |
| `0x8000` | `build/partition_table/partition-table.bin` | Project partition table. |
| `0xd000` | `build/ota_data_initial.bin` | Initial OTA selection data. |
| `0x10000` | `build/pocket_journal.bin` | App image; this is the image type used by signed OTA, with valid sidecars. |
| n/a | `build/flasher_args.json` and `build/flash_args` | Machine-readable and command-line flash maps generated by ESP-IDF. |

Do not write `pocket_journal.bin` at address `0x0`. Use `idf.py flash`, the
offsets above, or the merged public factory image.

### Erase, flash, and monitor

Set a convenient task-specific port variable on macOS/Linux:

```sh
PJ_PORT=/dev/cu.usbmodem1101
```

An ordinary source flash writes the generated images at their configured
offsets:

```sh
idf.py -p "$PJ_PORT" flash
```

Unlike the merged factory image, the normal separate-image flow does not write
the NVS partition at `0x9000`. To deliberately wipe all internal flash first:

```sh
idf.py -p "$PJ_PORT" erase-flash
idf.py -p "$PJ_PORT" flash
```

`erase-flash` removes firmware, internal settings, Wi-Fi credentials, tokens,
and OTA state. It does not erase the TF card.

Open serial logs only for a bounded diagnostic session:

```sh
idf.py -p "$PJ_PORT" monitor
```

Exit with `Ctrl+]`. You may combine a source flash and monitor:

```sh
idf.py -p "$PJ_PORT" flash monitor
```

Stop the monitor before any `pj` USB-C command; the monitor and partner cannot
share the port.

## Signed OTA builds

Release `0.0` is a fresh, ordinary unsigned factory build. It intentionally
fails closed with `ota.write=false`; it cannot be passed to `pj firmware
update`. Future OTA activation requires both independent trust paths:

- ESP-IDF signed-app verification, enabled by adding
  `sdkconfig.ota-signed.defaults` to `SDKCONFIG_DEFAULTS` and supplying an
  untracked `CONFIG_SECURE_BOOT_SIGNING_KEY` for the RSA image signature; and
- a trusted P-256 manifest public key in
  `CONFIG_PJ_OTA_TRUSTED_PUBLIC_KEY_HEX`, encoded as DER SubjectPublicKeyInfo
  hex. The corresponding ECDSA private key signs the canonical sidecar manifest
  and must remain outside the repository.

Keep both private keys outside the repository and release artifacts. A missing
or invalid key, failed known-vector self-test, unsigned ESP image, missing
provisioned bearer token, or ordinary build leaves OTA read-only. Use an
untracked sdkconfig fragment for key paths and public material:

```sh
idf.py -B build-ota \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ota-signed.defaults;/absolute/path/ota-keys.defaults' \
  -D PROJECT_VER=0.0.1 build
```

The USB-flashed factory image must already contain the intended OTA trust
configuration; the public fail-closed `0.0` factory image cannot bootstrap OTA
trust and must first be replaced over USB-C by a trust-configured factory build.
Versions supplied to signed OTA must be increasing strict `X.Y.Z` semantic
versions. A trust-configured factory build that retains embedded version
`0.0.0` can accept `0.0.1` as its first signed successor.

The first update from factory targets `ota_0` and relies on the intact factory
partition for rollback. Leave factory intact through first-OTA confirmation.
The repository validates OTA bundles but does not expose a CLI that creates or
signs their sidecars. `pj firmware update --file app.bin --yes` expects
`app.bin.manifest.json` and `app.bin.sig` unless explicit paths are supplied.

## Configure local transcription

The default backend is the external CPU-only whisper.cpp 1.9.1 `whisper-cli`
with the pinned `ggml-base.en-q5_0.bin` model. Setup is explicit; sync never
downloads a model implicitly.

With existing artifacts:

```sh
pj transcription setup \
  --runtime /usr/local/bin/whisper-cli \
  --runtime-sha256 SHA256 \
  --runtime-source SOURCE \
  --runtime-license MIT \
  --model /models/ggml-base.en-q5_0.bin
pj transcription status --digest
```

The exact Apple Silicon Homebrew executable used by the repository benchmark is
recognized from `PATH`; macOS can then derive the pinned model deliberately:

```sh
pj transcription setup --download-model
```

Pinned upstream runtime archives are supported on Linux x86-64/AArch64 and
Windows x86-64:

```sh
pj transcription setup --download-runtime --download-model
```

Those flags are the only setup operations that access the network. Downloads,
extraction, and quantization are bounded and digest-verified before atomic
installation. The official whisper.cpp 1.9.1 release has no macOS CLI archive;
install it locally and supply its provenance when it is not the recognized
Homebrew build. See [Transcription Benchmark](transcription-benchmark.md) for
the evidence procedure.

## Troubleshooting

### No serial port appears

- Confirm that the cable carries data and try another USB port without a hub.
- Keep AUX/BOOT released and power-cycle the board.
- Run `python -m serial.tools.list_ports -v` before and after connecting it.
- On Linux, check ownership and dialout-group membership for `/dev/ttyACM*`.
- On Windows, inspect Device Manager for the current `COM` number.

### Port busy or `pj` times out

Exit `idf.py monitor` with `Ctrl+]` and stop any other serial terminal or
running companion. Then run:

```sh
pj device usb-recover --probe-only
pj device usb-recover
```

Recovery probes before resetting and releases the port on completion, timeout,
error, or interruption. If both the application protocol and USB-JTAG are
silent, unplug and reconnect USB-C or use the dedicated reset control with
AUX/BOOT released.

### Device waits for download

Logs containing `boot:0x0 (DOWNLOAD(USB/UART0))` and `waiting for download`
mean GPIO0/AUX was held during reset. Stop the monitor and reset or power-cycle
with AUX/BOOT released. Hold AUX only when intentionally entering the ROM
loader for flashing.

### Firmware does not build

- Confirm `idf.py --version` reports 6.0.1 and the ESP-IDF environment is active.
- Run `idf.py set-target esp32s3` after switching targets.
- Use a clean build directory or `idf.py fullclean` after configuration changes.
- Keep network access available on the first build so the locked managed
  components can be fetched.
- Read the final app-size gate message: every 2 MiB app slot must retain the
  configured headroom for signed-image padding and future fixes.

### Provisioning or LAN discovery fails

- Quote SSIDs/passwords containing spaces or shell characters.
- Use `--serial-port PORT` when multiple USB serial devices are connected.
- Use `pj device wifi-diagnostics` for credential-safe connection state.
- Confirm the host and device are on the same trusted network for mDNS/LAN use.
- Pass `--device ID` when more than one paired device is configured.
- Pass `--base-url http://DEVICE_IP` only when mDNS is unavailable and the
  paired token is still valid.

### Transcription is unavailable

Run `pj transcription status --digest`. It reports missing paths, unsupported
runtime versions, and digest mismatches. Re-run `pj transcription setup` after
moving or replacing either artifact; the partner intentionally refuses silently
changed runtimes or models.

## Simulator

A fresh clone needs an active Emscripten toolchain so `emcc` can compile the
firmware UI core to WebAssembly:

```sh
make simulator
```

Open `http://127.0.0.1:8765`. If generated WASM already exists locally,
`make simulator` reuses it when `emcc` is unavailable. See
[Simulator](simulator.md).
