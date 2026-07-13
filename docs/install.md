# Install and Flashing

## Firmware

Use ESP-IDF for firmware development.

Expected flow after ESP-IDF is installed:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

The initial firmware app compiles the shared UI core and starts placeholder service boundaries. Hardware drivers should be enabled only after board revision verification.

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
pip install -e '.[ble,calendar,transcription]'
```

For source-tree runs before installation:

```sh
cd partner
PYTHONPATH=src python -m pocket_journal_partner --help
```

After flashing, you can sync the device clock from the computer that built/flashed the firmware:

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
