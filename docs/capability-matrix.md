# Partner CLI Capability Matrix

The `pj` companion uses JSON on stdout for successful operations and concise,
actionable errors on stderr. LAN/Wi-Fi requests use the per-device bearer token
stored during provisioning. Tokens, Wi-Fi passwords, and SSIDs are never included
in command output or errors.

## Capability Matrix

| Capability | CLI command | USB-C | LAN/Wi-Fi | Authentication | Firmware/API prerequisite | Mutable | Retry/idempotency |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Device discovery | `pj discover` | No | Yes, mDNS | None for discovery; API remains authenticated | `_pocket-journal._tcp` advertisement | No | Safe to repeat |
| Device status | `pj device status [--device ID]` | `PJ_STATUS` | `GET /v1/status` | Physical USB access or bearer token | v0 status command/endpoint | No | Safe to repeat |
| Wi-Fi diagnostics | `pj device wifi-diagnostics [--device ID]` | Normalizes `PJ_STATUS` | Normalizes `GET /v1/status` | Physical USB access or bearer token | Structured fields are used when firmware advertises them; older status is classified conservatively | No | Safe to repeat; output is credential-safe |
| USB recovery | `pj device usb-recover [--probe-only]` | Probes `PJ_STATUS` and ESP32-S3 ROM output; attempts an esptool USB-JTAG watchdog reset, then a short RTS fallback | No | Physical USB access | ESP32-S3 USB Serial/JTAG; physical re-enumeration if serial and JTAG are both silent | Resets the device only when the application does not answer | Bounded; reaps the esptool child, releases the port, and leaves DTR/RTS idle |
| Wi-Fi provisioning | `pj provision ... [--ble \| --serial-port PORT]` | `PJ_WIFI_HEX` (default) | No; BLE is an optional provisioning transport | Physical USB access or encrypted paired BLE; generates a new bearer token | v0 USB command or Pocket Journal BLE GATT service | Yes | Repeating intentionally replaces credentials and token |
| Time sync | `pj device sync-time [--device ID]` | `PJ_TIME` | `PUT /v1/time` | Physical USB access or bearer token | v0 time command/endpoint | Yes | Safe to repeat; USB provisioning performs it automatically and validates the echoed civil time against a host UTC anchor |
| Read settings | `pj settings get [--device ID]` | No | `GET /v1/settings` | Bearer token | v0 settings endpoint | No | Safe to repeat |
| Update settings | `pj settings set [--device ID] KEY=VALUE...` | No | `PUT /v1/settings` | Bearer token | v0 settings endpoint | Yes | Safe to retry; updates are atomic values |
| Read static art | `pj static-art get [--device ID]` | No | `GET /v1/static-art` | Bearer token | v0 static-art endpoint | No | Safe to repeat |
| Update static art | `pj static-art set [--device ID] --file FILE` | No | `PUT /v1/static-art` | Bearer token | v0 static-art endpoint | Yes | Safe to retry; replaces the complete bitmap |
| Read home layout | `pj home get [--device ID]` | No | `GET /v1/home` | Bearer token | v0 home endpoint | No | Safe to repeat |
| Update home layout | `pj home set [--device ID] --file FILE` | No | `PUT /v1/home` | Bearer token | v0 home endpoint | Yes | Safe to retry; replaces the complete layout |
| List audio | `pj recordings list [--device ID]` | No | `GET /v1/audio` | Bearer token | v0 audio endpoint | No | Safe to repeat |
| Download audio | `pj recordings download [--device ID] --audio-id ID [--output-dir DIR]` | No | `GET /v1/audio/{id}` | Bearer token | v0 audio endpoint | Writes only the selected local file | Safe to retry; replaces the same local filename |
| Sync audio and transcripts | `pj sync [--device ID] [--backend hf\|fake]` | No | Audio GET plus transcript PUT | Bearer token | v0 audio and transcript endpoints; selected transcription backend installed | Yes | Safe after interruption; cached audio/transcripts are reused and firmware skips uploaded notes |
| Upload transcript | Performed by `pj sync` | No | `PUT /v1/transcripts/{id}` | Bearer token | v0 transcript endpoint and an existing recording | Yes | Safe to retry for the same recording |
| Delete recordings | `pj recordings wipe [--device ID] --yes` | Tagged `PJ_WIPE_RECORDINGS` start plus `PJ_STATUS` polls | `DELETE /v1/audio` plus `GET /v1/status` polls | Physical USB access or bearer token | Async wipe operation status | Yes, destructive | Requires `--yes`; concurrent starts attach to one operation; terminal counts and retryability are reported |
| Speaker diagnostic | `pj device tone ...` | `PJ_AUDIO_TONE` | No | Physical USB access | v0 diagnostic command | Temporarily changes diagnostic routing | Safe to repeat; firmware restores temporary overrides |
| Microphone diagnostic | `pj device mic-check ...` | `PJ_MIC_CHECK` | No | Physical USB access | v0 diagnostic command | No retained recording | Safe to repeat |
| OTA firmware update | None | Not implemented | Reserved `POST /v1/ota`; not production-ready | Not applicable | Rollback and version verification are required first | Yes | Undefined; CLI intentionally refuses by having no command |

## Transport Selection

Commands that support both transports select LAN/Wi-Fi when `--device`,
`--base-url`, or `--token` is provided. Otherwise they prefer an unambiguous USB-C
device and fall back to LAN when no USB serial device is present. LAN-only commands
select a single complete paired profile directly, or join mDNS discovery with the
stored token when provisioning did not know the eventual IP address. Zero and
multiple candidates produce deterministic guidance. `--base-url`, `--token`,
`--serial-port`, `--serial-baud`, and `--timeout` retain explicit control. Treat
command-line token overrides as sensitive because the operating system may expose
process arguments.

USB serial commands request exclusive ownership, set DTR and RTS while the port is
still closed, and release the descriptor in every success, timeout, error, and
interrupt path. Ordinary commands never intentionally toggle those lines into an
ESP32 download/reset sequence. The explicit `pj device usb-recover` command first
probes for a firmware response or recognizable ROM boot log. For an unresponsive
or ROM-mode device it attempts a bounded esptool USB-JTAG watchdog reset, then a
short RTS fallback if necessary. Timeout and interruption paths terminate, wait
for, and if necessary kill and reap the esptool child before releasing the port.
If neither serial nor USB-JTAG answers, use a dedicated reset or unplug/replug the
board with AUX/BOOT released; hold AUX/BOOT only to enter ROM download mode.

Recording wipes use a bounded serial exchange for the start and each status poll;
no descriptor is held for the lifetime of the device operation. Start retries reuse
one request id so a lost acknowledgment attaches to the same operation, including
after terminal completion. Polls use fresh request ids and accept only responses
whose command tag, request id, and operation id match. A timeout reports the
operation id and does not imply that the device worker was cancelled.

The shared operation session reports transport as `"usb"` or `"lan"`, preflights
mutations, honors a firmware `capabilities` advertisement when present, and rejects
an explicit incompatible `api_version`. Current v0 firmware does not advertise
these optional fields, so the versioned `/v1` contract remains the compatibility
baseline.

## Output And Exit Codes

Successful device operations use one envelope:

```json
{
  "device_id": "pj-123abc",
  "transport": "lan",
  "result": {}
}
```

Provisioning output confirms the device id, BLE name, base URL, and completion,
but never returns the generated bearer token. It is stored only in the local
partner configuration, which is atomically replaced with owner-only (`0600`)
permissions.

| Exit code | Meaning |
| --- | --- |
| `0` | Operation completed |
| `1` | Device, transport, authentication, capability, or runtime failure |
| `2` | Invalid command-line syntax or missing required option |
| `130` | Interrupted with Ctrl-C |

HTTP `401`, `404`, and `409` responses are mapped to authentication,
unsupported-capability, and device-busy guidance. The CLI does not include response
bodies in errors, preventing device-provided content from leaking secrets.

## Examples

```sh
pj discover
pj device status
pj device wifi-diagnostics
pj device usb-recover
pj device sync-time
pj settings set volume=8 theme=dark
pj recordings list
pj recordings download --audio-id rec-001.wav --output-dir ./audio
pj sync --backend hf
pj recordings wipe --yes
```

USB-C serial support is included in the standard partner CLI install. Install optional transports and integrations from `partner/` as needed:

```sh
pip install -e '.[ble]'
pip install -e '.[transcription]'
```

## Deprecated Compatibility

`pj calendar sync` is hidden from primary help and emits a deprecation warning.
It remains compatible through `0.1.x` and will be removed in `0.2.0`. Install the
`calendar` extra only when maintaining existing automation during that window.
