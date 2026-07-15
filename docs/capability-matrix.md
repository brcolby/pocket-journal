# Partner CLI Capability Matrix

The `pj` companion uses JSON on stdout for successful operations and concise,
actionable errors on stderr. Interactive LAN/Wi-Fi commands use the per-device
bearer token stored during provisioning. Device-initiated sync instead uses mutual
domain-separated HMAC control envelopes and a temporary method/path-scoped data
credential; the raw token is not sent on that control path. Plain HTTP does not
encrypt LAN metadata or payloads. Tokens, Wi-Fi passwords, and SSIDs are never
included in command output or errors.

## Capability Matrix

| Capability | CLI command | USB-C | LAN/Wi-Fi | Authentication | Firmware/API prerequisite | Mutable | Retry/idempotency |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Device discovery | `pj discover` | Enumerates candidate serial ports without opening them | Yes, mDNS | None for discovery; API remains authenticated | USB CDC/JTAG enumeration or `_pocket-journal._tcp` advertisement | No | Safe to repeat |
| Device status | `pj device status [--device ID]` | `PJ_STATUS` | `GET /v1/status` | Physical USB access or bearer token | v0 status command/endpoint | No | Safe to repeat |
| Wi-Fi diagnostics | `pj device wifi-diagnostics [--device ID]` | Normalizes `PJ_STATUS` | Normalizes `GET /v1/status` | Physical USB access or bearer token | Structured fields are used when firmware advertises them; older status is classified conservatively | No | Safe to repeat; output is credential-safe |
| USB recovery | `pj device usb-recover [--probe-only]` | Probes `PJ_STATUS` and ESP32-S3 ROM output; attempts an esptool USB-JTAG watchdog reset, then a short RTS fallback | No | Physical USB access | ESP32-S3 USB Serial/JTAG; physical re-enumeration if serial and JTAG are both silent | Resets the device only when the application does not answer | Bounded; reaps the esptool child, releases the port, and leaves DTR/RTS idle |
| Wi-Fi provisioning | `pj provision ... [--ble \| --serial-port PORT]` | `PJ_WIFI_HEX` (default) | No; BLE is an optional provisioning transport | Physical USB access or encrypted paired BLE; generates a new bearer token | v0 USB command or Pocket Journal BLE GATT service | Yes | Repeating intentionally replaces credentials and token |
| Time sync | `pj device sync-time [--device ID]` | `PJ_TIME` with optional UTC-offset minutes | `PUT /v1/time` | Physical USB access or bearer token | Time command/endpoint | Yes | Safe to repeat; USB provisioning performs it automatically, persists the host offset, and validates echoed civil time against a host UTC anchor |
| Read settings | `pj settings get [--transport usb\|lan]` | `PJ_SETTINGS_GET` | `GET /v1/settings` | Physical USB access or bearer token | Generation-aware settings command/endpoint | No | Safe to repeat |
| Update settings | `pj settings set [--transport usb\|lan] KEY=VALUE...` | `PJ_SETTINGS_SET` with hex-safe JSON | `PUT /v1/settings` | Physical USB access or bearer token | Generation-aware settings command/endpoint | Yes | Atomic; stale generations fail without mutation and lost USB acknowledgments replay safely |
| List audio | `pj recordings list [--transport usb\|lan]` | Snapshot-paged `PJ_AUDIO_LIST`, one hex-safe item per response | `GET /v1/audio` | Physical USB access or bearer token | USB transfer protocol or v0 audio endpoint | No | Safe to repeat; snapshot changes abort the list |
| Download audio | `pj recordings download --audio-id ID [--transport usb\|lan]` | Chunked `PJ_AUDIO_READ`, 256 bytes per response | `GET /v1/audio/{id}` | Physical USB access or bearer token | USB transfer protocol or v0 audio endpoint | Writes only the selected local file | USB uses temp-file replace after size, offset, EOF, and digest validation; full sync also validates the WAV structure |
| Sync audio and transcripts | `pj sync [--transport usb\|lan] [--backend whisper-cpp\|hf\|fake]` | Snapshot list, chunked reads, atomic transcript upload | Audio GET plus transcript PUT | Physical USB access or bearer token | USB transfer protocol or v0 audio/transcript endpoints; selected local backend installed | Yes | Safe after interruption; cached audio/transcripts are reused and firmware skips uploaded notes |
| Device-initiated sync | `pj companion serve [--device ID] [--serial-port PORT]` | Polls durable `PJ_SYNC_STATUS`, conditionally claims a generation, then uses bounded audio/transcript commands; descriptors close between polls | Advertises `_pj-companion._tcp` API 2, exchanges peer-IP- and fresh-challenge-bound mutual HMAC envelopes, then uses a temporary credential limited to audio GET and transcript PUT | Physical USB access or provisioned-token-derived request/response/data keys; raw token is not sent on the control path | Paired profile, running local transcription backend, and either USB-C or configured LAN; HTTP is authenticated but unencrypted | Yes | Checksummed NVS generation plus pairing-epoch companion high-water; terminal results are served only after exact durable commit; restart/replay safe; an older completion never clears a newer request |
| Upload transcript | Performed by `pj sync` | `PJ_TRANSCRIPT_BEGIN/WRITE/COMMIT`, with best-effort abort | `PUT /v1/transcripts/{id}` | Physical USB access or bearer token | USB transfer protocol or v0 transcript endpoint and an existing recording | Yes | Digest-verified atomic commit; request-tagged chunks and commit are idempotent |
| Delete recordings | `pj recordings wipe [--device ID] --yes` | Tagged `PJ_WIPE_RECORDINGS` start plus `PJ_STATUS` polls | `DELETE /v1/audio` plus `GET /v1/status` polls | Physical USB access or bearer token | Async wipe operation status | Yes, destructive | Requires `--yes`; concurrent starts attach to one operation; terminal counts and retryability are reported |
| Speaker diagnostic | `pj device tone ...` | `PJ_AUDIO_TONE` | No | Physical USB access | v0 diagnostic command | Temporarily changes diagnostic routing | Safe to repeat; firmware restores temporary overrides |
| Microphone diagnostic | `pj device mic-check ...` | `PJ_MIC_CHECK` | No | Physical USB access | v0 diagnostic command | No retained recording | Safe to repeat |
| OTA firmware update | `pj firmware status/update` | No | Versioned OTA status and image stream | Bearer token plus explicit `--yes` activation | Rollback/version verification firmware | Yes | Preflight, digest verification, reconnect, and confirmed/rollback outcome |
| Local note library | `pj library list/show/title` | Not a device operation | Not a device operation | Local user account | SQLite schema migrations | Local title only | Stable `(device_id, audio_id)` identity; imports legacy jobs idempotently |
| Terminal/browser library | `pj library tui/serve` | Not a device operation | Loopback HTTP only | Local user account plus per-process CSRF token for writes | Local library | Local title only | Browser audio uses validated identities and byte ranges; non-loopback binds and DNS-rebinding host headers are refused |

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

USB note transfers never place arbitrary UTF-8 or binary bytes directly on a
console line. Identifiers and payload chunks are hex encoded, current recording
identifiers are bounded to 95 UTF-8 bytes, audio chunks are bounded to 256 bytes,
transcript chunks are bounded to 192 bytes,
lists return at most one item per response, and transcripts are limited to 64
KiB. Firmware reads title metadata across that same full bound, including titles
located after the first 8 KiB. Audio reads validate a stable snapshot/source digest and exact offsets.
Transcript uploads validate declared byte count and SHA-256 before an atomic
commit; failures trigger a best-effort abort without treating an unknown commit
outcome as success.

Settings reads return the canonical persisted fields plus a monotonic
`generation`. Both transports pin that generation for partial updates. The USB
command hex-encodes at most 256 bytes of compact JSON, rejects unknown or removed
fields, and replays an identical last request id after a lost acknowledgment;
reusing an id with different content fails closed. LAN sends the same expected
generation in the request body and returns `409` on a stale value.

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
pj transcription status --model /models/ggml-base.en-q5_0.bin
pj sync --model /models/ggml-base.en-q5_0.bin
pj library tui
pj library serve
pj recordings wipe --yes
```

USB-C serial support is included in the standard partner CLI install. Install optional transports and integrations from `partner/` as needed:

```sh
pip install -e '.[ble]'
pip install -e '.[transcription]'
```
