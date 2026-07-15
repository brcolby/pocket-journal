# Device-Initiated Sync

The Settings **Sync** action durably queues the recording download, local library,
transcription, and transcript upload pipeline. The request survives navigation,
sleep, an offline laptop, and device reboot. Run the foreground companion before or
after selecting Sync:

```sh
pj transcription status --model /models/ggml-base.en-q5_0.bin
pj companion serve --model /models/ggml-base.en-q5_0.bin
```

When more than one Pocket Journal is paired, select the profile explicitly:

```sh
pj companion serve --device pj-abcdef --model /models/ggml-base.en-q5_0.bin
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
version 2, `hmac-sha256-v1` authentication, and the `/v1/sync` path. Use
`--port` to change the listener port,
`--advertise-address` to select a LAN IPv4 interface, or `--no-mdns` for USB-only
diagnostics.

The device accepts exactly one matching advertisement. It never sends the raw
provisioned token on the device-initiated control path. Instead, the device and
companion derive separate request, response, and data keys with domain-separated
HMAC-SHA256. Signed request envelopes bind the action, device id, canonical device
IPv4 address, operation id, generation, request timestamp, and a fresh 128-bit
challenge. The companion
requires the signed device address to equal the TCP peer address. Signed responses
bind the same request identity and challenge plus the exact state, counts, and
bounded error. A captured response therefore cannot satisfy a later status poll.
Both sides compare MACs in constant time and reject unknown fields, stale identity,
tampering, and the unprovisioned development token.

After a valid claim, the companion derives a temporary operation credential for
the device data plane. Firmware accepts that credential only for `GET /v1/audio`,
`GET /v1/audio/{id}`, and `PUT /v1/transcripts/{id}` while the matching LAN claim
is active. It cannot authorize recording deletion, settings, status, OTA, or any
other route, and it stops working when the claim is released or completed.

LAN sync uses plain HTTP. The HMAC envelopes authenticate the control messages but
do not encrypt LAN metadata, audio, transcripts, or the temporary data credential;
the data payloads also lack end-to-end protection from an active on-path attacker.
Use USB-C on an untrusted network, or run LAN sync only on a trusted local network.

## Durability And Recovery

Every device action increments a checksummed NVS request generation before the UI
returns. Claims and exact terminal outcomes are also persisted transactionally;
frequent progress counters remain in RAM to avoid flash wear. `PJ_STATUS`, `PJ_SYNC_STATUS`, and
`GET /v1/status` expose the same requested, acknowledged, active, phase, transport,
counts, and credential-free error state.

USB-C and LAN conditionally claim the exact advertised generation and operation ID.
An interrupted claim becomes reclaimable after reboot without changing its
operation id, generation, or request timestamp. The companion durably records the
accepted generation, request identity, last restart-triggering challenge, and
exact terminal outcome. After a restart, a matching status request resumes an unfinished
idempotent file/library pipeline or replays the durable terminal outcome without
starting another worker. A terminal response is withheld until that exact outcome
is on disk. Reprovisioning creates a new token-derived pairing epoch, so a clean
device whose generation restarts at one is not rejected by the prior pairing's
high-water. A conflicting completion or failure is rejected. Completion of an
older active generation cannot clear a newer device
request. Offline requests remain pending; authentication and malformed-protocol
failures remain visible for explicit action. A failed NVS terminal commit restores
the pre-acknowledgement state so the same operation can retry.

The Sync screen reports `PENDING`, `ACTIVE`, `COMPLETE`, `OFFLINE`, or `FAILED`
from protocol state, plus transferred, pending, failed, and a bounded actionable
error. Work remains asynchronous on both the device and companion.
