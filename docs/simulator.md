# Simulator

The simulator is a static browser app for pre-hardware visual iteration.

It renders a 200x200 black/white canvas scaled up for inspection. Touch events are mapped to the same named states used by firmware. The state model is intentionally kept isomorphic with the C state machine so UI decisions can be validated before hardware arrives.

Run it:

```sh
make simulator
```

Open:

```text
http://127.0.0.1:8765
```

## Interaction Model

- `static`: tap to `time_temp`.
- `time_temp`: tap top half to `static`, bottom half to `home`.
- `home`: five rows for notes, time, settings, calendar, TBD.
- `notes`: record, listen, read.
- `time`: alarm, stopwatch, timer, interval.
- `settings`: sync, volume.
- Leaf screens: tap top-left back target to return to parent.

