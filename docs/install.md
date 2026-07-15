# Install and Flashing

## Firmware

Use ESP-IDF for firmware development.

Expected flow after ESP-IDF is installed:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py flash
```

The firmware app compiles the shared UI core and starts the board display,
input, storage, audio, connectivity, and partner-service integrations.

### Signed OTA Builds

The ordinary `sdkconfig.defaults` build intentionally reports `ota.write=false`.
OTA activation requires both independent trust paths:

- ESP-IDF signed-app update verification, enabled by adding
  `sdkconfig.ota-signed.defaults` to `SDKCONFIG_DEFAULTS` and supplying an
  untracked `CONFIG_SECURE_BOOT_SIGNING_KEY` for the RSA image signature.
- A trusted P-256 manifest public key in
  `CONFIG_PJ_OTA_TRUSTED_PUBLIC_KEY_HEX`, encoded as DER SubjectPublicKeyInfo
  hex. The corresponding ECDSA private key signs the canonical sidecar manifest
  and must remain outside the repository.

Keep both private keys outside the repository and release artifacts. A missing
or invalid key, failed known-vector self-test, unsigned ESP image, missing
provisioned bearer token, or ordinary non-signed build leaves OTA read-only.
Use an untracked sdkconfig fragment for the key paths/material, for example:

```sh
idf.py -B build-ota \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ota-signed.defaults;/absolute/path/ota-keys.defaults' \
  -D PROJECT_VER=1.0.0 build
```

The USB-flashed factory image must already contain this OTA trust configuration;
an ordinary fail-closed factory image cannot bootstrap OTA. The first update
from factory targets `ota_0` and relies on the intact factory partition as its
rollback image. Leave factory intact through first-OTA confirmation. A factory
development/git version gets one migration to a strict `X.Y.Z` version; all
subsequent OTA releases must remain increasing strict semver and alternate the
OTA slots. `pj firmware update --file app.bin --yes` expects
`app.bin.manifest.json` and `app.bin.sig` unless explicit sidecar paths are
provided.

Use `idf.py monitor` only for an explicit serial-log session. Stop it with
`Ctrl+]` as soon as that session is complete and before running any `pj` USB-C
command; the monitor and partner CLI cannot share `/dev/cu.usbmodem*`.

On the current board, BOOT/AUX is GPIO0 and is also the ESP32-S3 ROM download
strap. If serial logs show `boot:0x0 (DOWNLOAD(USB/UART0))` and `waiting for
download`, stop the monitor and recover with BOOT/AUX released. Hold BOOT/AUX
during reset only when intentionally entering ROM download mode.

## Partner CLI

From the repo:

```sh
cd partner
python -m venv .venv
. .venv/bin/activate
pip install -e '.[dev]'
pj --help
```

Optional extras:

```sh
pip install -e '.[ble,transcription]'
```

The default transcription runtime is the external CPU-only `whisper.cpp`
`whisper-cli`, so it does not require the Python `transcription` extra. Install
that runtime separately, obtain a local `ggml-base.en-q5_0.bin` model deliberately,
and point the partner at it without an implicit model download:

```sh
pj transcription status --model /models/ggml-base.en-q5_0.bin --digest
pj transcription benchmark --manifest /path/to/manifest.json --model /models/ggml-base.en-q5_0.bin --output /path/to/report.json
pj sync --model /models/ggml-base.en-q5_0.bin
pj library tui
pj library serve
```

Set `PJ_WHISPER_CPP` and `PJ_WHISPER_MODEL` to retain the executable/model choices.
The web library binds only to `127.0.0.1` by default.
The benchmark command and cross-platform evidence procedure are documented in
[Transcription Benchmark](transcription-benchmark.md).

For source-tree runs before installation:

```sh
cd partner
PYTHONPATH=src python -m pocket_journal_partner --help
```

USB-C provisioning also sets and validates the device's local civil time from the
host, with its corresponding UTC anchor included in the command result. You can
retry that operation explicitly after flashing:

```sh
cd partner
pj provision --ssid <ssid> --password <password>
pj device sync-time
```

Provisioning and USB maintenance commands auto-detect `/dev/cu.usbmodem1101` or the single attached USB serial port. Pass `--serial-port` only to override the detected port, or pass `--ble` to provision wirelessly.
Stop `idf.py monitor` first, because the USB serial port cannot be shared.
If the command times out even with no monitor running, rebuild/flash firmware with USB Serial/JTAG selected as the primary console input.
With AUX/BOOT released, run `pj device usb-recover`. It probes before resetting,
then attempts a bounded esptool USB-JTAG watchdog reset followed by a short RTS
fallback when needed. The command always reaps its esptool child and releases the
serial port on completion, timeout, error, or interruption. Add `--probe-only` to
inspect without resetting.

If both the serial application protocol and USB-JTAG remain silent, software
cannot recover the link. Physically re-enumerate the board with its dedicated
reset control or by unplugging and reconnecting USB-C, keeping AUX/BOOT released
for normal boot.

## Simulator

```sh
make simulator
```

Open `http://127.0.0.1:8765`.
