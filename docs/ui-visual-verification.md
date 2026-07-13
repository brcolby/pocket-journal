# UI Visual Verification

Generate deterministic 200x200 PNG captures from the firmware-backed WASM renderer:

```sh
make ui-gallery
```

The output is written to `build/ui-gallery/`. `gallery.png` is a contact sheet for visual review; 22 individual PNGs cover primary menus, both clock/unit modes, 12-hour alarm disambiguation, recording, empty and populated note lists, playback and transcript detail, time tools, the flattened Settings rows in light/dark and 12/24-hour states, and the volume screen. `manifest.json` records pixel metrics for each frame.

Generation fails when a frame is blank, has unstable dimensions, has extreme ink density, or is almost entirely imbalanced into one half of the display. Screens with full-bleed controls additionally fail unless their control geometry reaches the expected display edges. Edge contact is intentional for contiguous controls, so clipping remains a visual-review concern rather than a blanket pixel prohibition. These checks catch rendering failures and gross layout regressions; they do not replace product-owner review of hierarchy, typography, control meaning, or e-paper ghosting.

Run the analysis unit tests independently with:

```sh
make test-ui-images
```
