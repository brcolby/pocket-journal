# Simulator

The simulator is a static browser app for pre-hardware visual iteration.

It renders a 200x200 black/white canvas scaled up for inspection. Touch events are mapped to the same named states used by firmware. The state model is intentionally kept isomorphic with the C state machine so UI decisions can be validated before hardware arrives.

The display canvas is treated as a hardware framebuffer:

- Canvas backing size is fixed at 200x200.
- The visible scale is an integer multiple of the panel size.
- Every render pass is thresholded to exact black or exact white pixels.
- Alpha, grayscale antialiasing, off-white paper colors, and off-black ink colors are removed from the final framebuffer.
- Browser-side vector drawing is allowed only as an intermediate step before the one-bit clamp.

The simulator renders screen text from generated 1-bit Space Mono Bold assets, matching the firmware renderer. The generation step reads `assets/fonts/SpaceMono-Bold.ttf`, thresholds the antialiased glyphs into black/white bitmap rows without dithering, writes `simulator/assets/fonts/space-mono-bold-1bit.json`, and writes the firmware header at `firmware/components/pj_ui/include/pj_font_space_mono.h`. Google Material Symbols are still loaded for simulator-only icon previews.

Regenerate font assets after changing the source TTF or logical size map:

```sh
make generate-font-assets
```

Run the simulator framebuffer contract test:

```sh
make test-simulator
```

For custom resting art, set `localStorage.pocketJournalStaticArt` to the same `/v1/static-art` JSON shape. If none is present, the simulator renders the default smiley bitmap. The generated raster source for that default is `assets/static/smiley.pbm`.

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
- `time_temp`: triangle returns to `static`; other taps go to `home`.
- `home`: five rows for notes, time, settings, calendar, TBD.
- `notes`: record, listen, read.
- `time`: alarm, stopwatch, timer, interval.
- `settings`: sync, volume.
- `settings`: sync, volume, dark/light toggle.
- `settings`: volume changes inline by tapping left/right side of the volume row.
- `sync`: tap to advance dummy pending/transferred counters; intended as a partial-refresh region.
- `volume`: tap left to lower, right to raise.
- `record`: entering starts recording; pause/resume/finish update only the control/status region.
- `listen` and `read`: show dummy recordings ordered newest first; tap a note to enter detail.
- All non-root screens use the upper-left triangle as the canonical back control.
