# Home layout

The home screen has a title and one to four ordered slots. A layout replaces the
complete active design; partial updates are not supported.

## API

`GET /v1/home` returns the active in-memory layout, which is the last layout
successfully committed to NVS or the built-in fallback:

```json
{
  "title": "Pocket Journal",
  "slots": [
    {"label": "Notes", "icon": "notebook", "state": "notes"},
    {"label": "Time", "icon": "time", "state": "time"},
    {"label": "Settings", "icon": "settings", "state": "settings"}
  ]
}
```

`PUT /v1/home` accepts the same shape. The title is 1-24 printable ASCII
characters. Each label is 1-12 printable ASCII characters. Outer whitespace is
rejected. Slots keep their submitted order and must contain exactly `label`,
`icon`, and `state`.

Supported icons are:

```text
alarm document_audio microphone notebook play read_me repeat settings
time timer volume_up wifi
```

Supported destinations (`state` values) are:

```text
notes record listen read time alarm stopwatch timer interval settings sync
volume calendar
```

These lists are deliberately closed: the firmware only accepts assets it can
render and states it can route. An invalid request returns HTTP 400 and leaves
the previous layout unchanged. A successful PUT commits the model first, then
queues its visual application for the main UI loop so the HTTP task never
renders the display.

Send `{"reset":true}` to `PUT /v1/home` to store the compiled fallback. The
same reset is available without Wi-Fi over USB-C by sending `PJ_HOME_RESET` and
waiting for `PJ_OK {"home_reset":true}`.

## Persistence and recovery

The device stores a 228-byte, versioned and checksummed record as one NVS blob.
NVS commit provides transactional replacement and wear leveling suitable for
infrequent layout edits. The firmware validates the complete candidate before
writing, so unsupported updates cannot replace the last known good layout.

At boot, a missing, truncated, checksum-invalid, version-invalid, or
model-invalid record is ignored. The compiled three-slot Notes/Time/Settings
layout is loaded instead, preserving local navigation even when Wi-Fi and the
partner are unavailable. `PJ_HOME_RESET` makes that recovery layout persistent.

## Hardware validation

After flashing, set a four-slot design, reboot and sleep/wake the device, and
confirm title, order, icons, labels, and all four touch targets. Then issue
`PJ_HOME_RESET` over USB-C and confirm the fallback layout appears without a
network connection.
