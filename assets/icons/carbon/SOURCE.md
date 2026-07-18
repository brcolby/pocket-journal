# Carbon Icons

The authoritative source is the official [Carbon icon
library](https://carbondesignsystem.com/elements/icons/library/) package
`@carbon/icons@11.82.0`.

The npm tarball `carbon-icons-11.82.0.tgz` has SHA-1
`c5ae9cc66e2698db1f05a8110e051c7b94eed8df`. Carbon is Apache-2.0 licensed;
the upstream license is checked in as `LICENSE`.

`manifest.json` records every per-file SHA-256, active/reference status,
typed firmware ID, authorized size, and letter extraction rule. It covers
exactly 99 SVGs: 73 active sources and 26 visual references. The four legacy
Arrow direction SVGs remain in the reference section for provenance but are
not compiled and cannot resolve through the typed asset API.

The exact compiled semantic pairs total 30: 13 launcher records at 64px, 11
control records at 40px, four battery records at 28px, and Listen-detail
Play/Pause at 96px. `VolumeUp` intentionally has both a launcher and a
control record. The generator also derives 52 case-preserving letters, 20
digit variants, and the 12H/24H Settings composites.

Install the pinned generator environment from
`tools/carbon-assets-requirements.lock`, then run:

```sh
python3 -m pip install --require-hashes \
  -r tools/carbon-assets-requirements.lock \
  --target /tmp/pj-carbon-python
PYTHONPATH=/tmp/pj-carbon-python python3 tools/generate_icon_assets.py
PYTHONPATH=/tmp/pj-carbon-python python3 tools/generate_icon_assets.py --check
```

Generation always uses the complete 32×32 upstream viewbox, CairoSVG 2.9.0
at 8×, Pillow 12.3.0 fixed Lanczos downsampling, luminance threshold 192, and
no dithering. `--check` validates the manifest and tool versions, generates
twice, and byte-compares all firmware, simulator, and gallery outputs.
