#!/usr/bin/env python3
"""Compatibility entry point for the consolidated Carbon text generator.

IBM Plex Mono Bold is no longer a full ASCII UI font.  The deterministic asset
pipeline emits only its space, punctuation, and unsupported-codepoint fallback
alongside Carbon's case-preserving letter and fixed-width digit glyphs.
"""

from generate_icon_assets import main


if __name__ == "__main__":
    raise SystemExit(main())
