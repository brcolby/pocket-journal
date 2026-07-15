# Static Art

The static screen renders one immutable 200x200 monochrome image compiled into
firmware. It does not load an override from storage and does not expose artwork
mutation through the partner CLI, LAN API, or USB-C protocol.

## Source And Generated Files

The product source is `assets/static/pocket-journal-default.png`, a 200x200,
8-bit grayscale, non-interlaced PNG without alpha. Its pixels are strictly
monochrome (`0` or `255`), and its SHA-256 is
`ef0fb9a1a2764e19d04056ee57bf9af0c86c2baba2aae098ed945dc07c0d4e9d`.

`tools/generate_static_art.py` validates and decodes the PNG without a
third-party image library. It treats grayscale values below 128 as black and
generates:

- `assets/static/pocket-journal-default.pbm`, a readable 1-bit reference used by
  simulator fidelity checks;
- `firmware/components/pj_ui/include/pj_default_static_art.h`, dimensions and
  generation metadata;
- `firmware/components/pj_ui/pj_default_static_art.c`, the compiled 5,000-byte
  bitmap.

The packed bitmap is row-major from the top-left. Black is `1`, and the first
pixel in each byte is bit 0. Regenerate and verify committed outputs with:

```sh
make generate-static-art
make check-static-art
```

## Rendering Fidelity

Firmware and simulator use the same compiled bytes without text or other
compositing. The firmware-backed runtime test compares all 40,000 rendered
pixels against the generated PBM and fails on rotation, mirroring, inversion, or
packing changes:

```sh
make test-simulator-runtime
```
