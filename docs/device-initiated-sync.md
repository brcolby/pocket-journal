# Device-Initiated Sync

The Settings **Sync** action durably queues the recording download, local library,
transcription, and transcript upload pipeline. The request survives navigation,
sleep, an offline laptop, and device reboot. Run the foreground companion before or
after selecting Sync:

```sh
pj transcription status --model /models/ggml-base.en.bin
pj companion serve --model /models/ggml-base.en.bin
```

When more than one Pocket Journal is paired, select the profile explicitly:

```sh
pj companion serve --device pj-abcdef --model /models/ggml-base.en.bin
```

## USB-C Default

`pj companion serve` polls USB-C every two seconds by default. Each poll resolves
the port, issues one bounded command, and closes the descriptor; it does not retain
the serial link between polls. Use `--serial-port PORT` to select a port,
`--usb-poll-interval SECONDS` to choose a value from 0.25 through 60, or `--no-usb`
for LAN-only diagnostics. A serial monitor still owns the same exclusive descriptor,
so close it while the companion is expected to service USB requests.

The control plane uses `PJ_SYNC_STATUS`, generation-conditional `PJ_SYNC_CLAIM`,
and `PJ_SYNC_PROGRESS`, `PJ_SYNC_COMPLETE`, or `PJ_SYNC_FAIL`. Audio transfer and
transcript upload reuse the bounded `PJ_AUDIO_*` and `PJ_TRANSCRIPT_*` commands.
Every command opens a short-lived connection. Per-port serialization prevents the
poller and data plane from opening the device concurrently.

## Authenticated LAN

The same process listens on TCP port `8765` and advertises
`_pj-companion._tcp.local.`. Its TXT record contains the paired `device_id`, API
version, and `/v1/sync` path. Use `--port` to change the listener port,
`--advertise-address` to select a LAN IPv4 interface, or `--no-mdns` for USB-only
diagnostics.

The device accepts exactly one matching advertisement. It authenticates POST and
bounded progress polls with the bearer token created during provisioning. The
companion compares that token in constant time, derives the device HTTP address
from the request source, and never accepts an arbitrary callback URL.

## Durability And Recovery

Every device action increments a checksummed NVS request generation before the UI
returns. Claim and terminal transitions are also persisted; frequent progress
counters remain in RAM to avoid flash wear. `PJ_STATUS`, `PJ_SYNC_STATUS`, and
`GET /v1/status` expose the same requested, acknowledged, active, phase, transport,
counts, and credential-free error state.

USB-C and LAN conditionally claim the exact advertised generation and operation ID.
An interrupted claim becomes reclaimable after reboot. A restarted companion uses
the same operation ID and reruns the idempotent file/library pipeline. Replayed
terminal acknowledgements are accepted, while completion of an older active
generation cannot clear a newer device request. Offline requests remain pending;
authentication and malformed-protocol failures remain visible for explicit action.

The Sync screen reports `PENDING`, `ACTIVE`, `COMPLETE`, `OFFLINE`, or `FAILED`
from protocol state, plus transferred, pending, failed, and a bounded actionable
error. Work remains asynchronous on both the device and companion.
