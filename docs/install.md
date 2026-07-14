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

The initial firmware app compiles the shared UI core and starts placeholder service boundaries. Hardware drivers should be enabled only after board revision verification.

Use `idf.py monitor` only when you explicitly need serial logs, and stop it
with `Ctrl+]` before running any `pj` USB-C command. The monitor and partner
CLI cannot share `/dev/cu.usbmodem*`.

On the current board, BOOT/AUX is GPIO0 and is also the ESP32-S3 ROM download
strap. If serial logs show `boot:0x0 (DOWNLOAD(USB/UART0))` and `waiting for
download`, stop the monitor and reset or power-cycle the board with BOOT/AUX
released. Do not hold BOOT/AUX during reset unless you intentionally want ROM
download mode.

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

## Simulator

```sh
make simulator
```

Open `http://127.0.0.1:8765`.
