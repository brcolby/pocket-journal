# UI Visual Verification

Generate deterministic 200x200 PNG captures from the firmware-backed WASM renderer:

```sh
make ui-gallery
```

The output is written to `build/ui-gallery/`. `gallery.png` is a contact sheet for visual review; individual PNGs cover primary menus, recording, empty and populated note lists, note detail, time tools, settings, sync, and volume. `manifest.json` records pixel metrics for each frame.

Generation fails when a frame is blank, has unstable dimensions, draws on the physical display edge (a clipping signal), contains a connected component spanning nearly the entire panel, has extreme ink density, or is almost entirely imbalanced into one half of the display. These checks catch rendering failures and gross layout regressions; they do not replace product-owner review of hierarchy, typography, control meaning, or e-paper ghosting.

Run the analysis unit tests independently with:

```sh
make test-ui-images
```
