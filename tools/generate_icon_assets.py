#!/usr/bin/env python3
"""Generate Pocket Journal's deterministic 1-bit Carbon asset set.

The checked-in manifest is the contract.  Normal generation never searches for
icons, guesses a size, crops an upstream viewbox, or substitutes text.  SVGs are
rasterized from their complete 32 by 32 viewbox at 8x, downsampled once with
Lanczos, composited on white, and thresholded at luminance 192 without dithering.

Run with the environment pinned by tools/carbon-assets-requirements.lock:

    PYTHONPATH=/path/to/site-packages python3 tools/generate_icon_assets.py
    PYTHONPATH=/path/to/site-packages python3 tools/generate_icon_assets.py --check
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import html
import io
import json
import string
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

import cairosvg
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
CARBON_DIR = ROOT / "assets/icons/carbon"
SOURCE_DIR = CARBON_DIR / "svg/32"
MANIFEST_PATH = CARBON_DIR / "manifest.json"
GALLERY_PATH = CARBON_DIR / "gallery.html"
FONT_PATH = ROOT / "assets/fonts/IBMPlexMono-Bold.ttf"
FONT_LICENSE_PATH = ROOT / "assets/fonts/IBMPlexMono-LICENSE.txt"
COMMON_HEADER = ROOT / "firmware/components/pj_ui/include/pj_asset_bitmap.h"
ICON_HEADER = ROOT / "firmware/components/pj_ui/include/pj_icon_carbon.h"
GLYPH_HEADER = ROOT / "firmware/components/pj_ui/include/pj_glyph_carbon.h"
PUNCT_HEADER = ROOT / "firmware/components/pj_ui/include/pj_font_ibm_plex_mono_bold.h"
SIM_ICON_OUTPUT = ROOT / "simulator/assets/icons/carbon-1bit.json"
SIM_GLYPH_OUTPUT = ROOT / "simulator/assets/fonts/carbon-glyphs-1bit.json"
SIM_PUNCT_OUTPUT = ROOT / "simulator/assets/fonts/ibm-plex-mono-bold-punctuation-1bit.json"

PACKAGE_NAME = "@carbon/icons"
PACKAGE_VERSION = "11.82.0"
PACKAGE_TARBALL_SHA1 = "c5ae9cc66e2698db1f05a8110e051c7b94eed8df"
PACKAGE_URL = "https://www.npmjs.com/package/@carbon/icons/v/11.82.0"
LIBRARY_URL = "https://carbondesignsystem.com/elements/icons/library/"
CAIROSVG_VERSION = "2.9.0"
PILLOW_VERSION = "12.3.0"
FONT_SHA256 = "74e5eedcfa4596497d34e19023cabdabd3a8c852b903007a5654a59591a72ffb"
FONT_VERSION = "2.005"
SUPERSAMPLE = 8
THRESHOLD = 192
TEXT_SIZES = (16, 24, 32, 64)
PAINT_TAGS = {"path", "rect", "circle", "ellipse", "line", "polyline", "polygon"}
PUNCTUATION = " " + string.punctuation


# Each semantic icon has exactly the sizes authorized by the UI contract.  The
# duplicate VolumeUp size is intentional: it is a 64px Settings launcher and a
# 40px Volume control.  These pairs total 30 compiled semantic records.
SEMANTIC_ICONS: tuple[tuple[str, str, tuple[int, ...]], ...] = (
    ("TIME", "time.svg", (64,)),
    ("DATA_ENRICHMENT", "data-enrichment.svg", (64,)),
    ("SERVICE_LEVELS", "service-levels.svg", (64,)),
    ("WAVEFORM", "waveform.svg", (64,)),
    ("HEARING", "hearing.svg", (64,)),
    ("VIEW_FILLED", "view--filled.svg", (64,)),
    ("CHEVRON_LEFT", "chevron--left.svg", (40,)),
    ("CHEVRON_RIGHT", "chevron--right.svg", (40,)),
    ("PLAY_FILLED", "play--filled.svg", (40, 96)),
    ("PAUSE_FILLED", "pause--filled.svg", (40, 96)),
    ("ALARM", "alarm.svg", (64,)),
    ("TIMER", "timer.svg", (64,)),
    ("HOURGLASS", "hourglass.svg", (64,)),
    ("REPEAT", "repeat.svg", (64,)),
    ("TOGGLE_ON", "toggle-on.svg", (40,)),
    ("TOGGLE_OFF", "toggle-off.svg", (40,)),
    ("CARET_UP", "caret--up.svg", (40,)),
    ("CARET_DOWN", "caret--down.svg", (40,)),
    ("RESET_ALT", "reset--alt.svg", (40,)),
    ("VOLUME_UP", "volume--up.svg", (40, 64)),
    ("VOLUME_DOWN", "volume--down.svg", (40,)),
    ("ASLEEP_FILLED", "asleep--filled.svg", (64,)),
    ("FETCH_UPLOAD", "fetch-upload.svg", (64,)),
    ("BATTERY_EMPTY", "battery--empty.svg", (28,)),
    ("BATTERY_LOW", "battery--low.svg", (28,)),
    ("BATTERY_HALF", "battery--half.svg", (28,)),
    ("BATTERY_FULL", "battery--full.svg", (28,)),
)


# These were the complete pre-overhaul non-active set.  They remain available
# for visual provenance only and are never emitted to firmware/simulator tables.
# In particular the four Arrow directions are deliberately reference-only.
REFERENCE_FILES: tuple[str, ...] = (
    "add.svg",
    "arrow--down.svg",
    "arrow--left.svg",
    "arrow--right.svg",
    "arrow--up.svg",
    "battery--charging.svg",
    "chevron--down.svg",
    "chevron--up.svg",
    "document--audio.svg",
    "document.svg",
    "home.svg",
    "list.svg",
    "microphone.svg",
    "notebook.svg",
    "pause.svg",
    "play.svg",
    "power.svg",
    "read-me.svg",
    "recording.svg",
    "reset.svg",
    "settings--adjust.svg",
    "settings.svg",
    "stop.svg",
    "subtract.svg",
    "time--filled.svg",
    "wifi.svg",
)


@dataclass(frozen=True)
class Bitmap:
    width: int
    height: int
    rows: tuple[str, ...]
    advance: int = 0
    x_offset: int = 0
    y_offset: int = 0

    @property
    def stride(self) -> int:
        return (self.width + 7) // 8

    @property
    def ink_bbox(self) -> tuple[int, int, int, int] | None:
        points = [
            (x, y)
            for y, row in enumerate(self.rows)
            for x, bit in enumerate(row)
            if bit == "1"
        ]
        if not points:
            return None
        xs = [point[0] for point in points]
        ys = [point[1] for point in points]
        return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def painted_elements(root: ET.Element) -> list[ET.Element]:
    return [element for element in root.iter() if local_name(element.tag) in PAINT_TAGS]


def selected_svg(svg_path: Path, element_indices: Sequence[int] | None = None) -> bytes:
    root = ET.fromstring(svg_path.read_bytes())
    if element_indices is None:
        return svg_path.read_bytes()
    elements = painted_elements(root)
    selected = set(element_indices)
    output = ET.Element(
        "svg",
        {
            "xmlns": "http://www.w3.org/2000/svg",
            "viewBox": root.attrib.get("viewBox", "0 0 32 32"),
        },
    )
    for index, element in enumerate(elements):
        if index in selected:
            output.append(copy.deepcopy(element))
    return ET.tostring(output, encoding="utf-8")


def render_rgba(svg_bytes: bytes, size: int) -> Image.Image:
    scale = size * SUPERSAMPLE
    png = cairosvg.svg2png(bytestring=svg_bytes, output_width=scale, output_height=scale)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def threshold_svg(svg_bytes: bytes, size: int) -> tuple[str, ...]:
    rgba = render_rgba(svg_bytes, size)
    white = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    white.alpha_composite(rgba)
    reduced = white.convert("L").resize((size, size), Image.Resampling.LANCZOS)
    # Explicit list conversion avoids Pillow mode-dependent dithering entirely.
    pixels = list(reduced.get_flattened_data())
    return tuple(
        "".join("1" if pixels[y * size + x] < THRESHOLD else "0" for x in range(size))
        for y in range(size)
    )


def bitmap_from_svg(svg_path: Path, size: int, elements: Sequence[int] | None = None) -> Bitmap:
    return Bitmap(size, size, threshold_svg(selected_svg(svg_path, elements), size))


def element_center(svg_path: Path, element_index: int) -> float:
    rgba = render_rgba(selected_svg(svg_path, (element_index,)), 32)
    bbox = rgba.getchannel("A").getbbox()
    if bbox is None:
        raise ValueError(f"{svg_path.name}: painted element {element_index} rendered empty")
    return (bbox[0] + bbox[2]) / (2.0 * SUPERSAMPLE)


def compute_letter_groups(svg_path: Path) -> tuple[tuple[int, ...], tuple[int, ...]]:
    root = ET.fromstring(svg_path.read_bytes())
    elements = painted_elements(root)
    if len(elements) < 2:
        raise ValueError(f"{svg_path.name}: letter pair has fewer than two painted elements")
    indexed_centers = sorted(
        ((element_center(svg_path, index), index) for index in range(len(elements))),
        key=lambda item: (item[0], item[1]),
    )
    gaps = [indexed_centers[index + 1][0] - indexed_centers[index][0] for index in range(len(indexed_centers) - 1)]
    split_index = max(range(len(gaps)), key=lambda index: (gaps[index], -index)) + 1
    left = tuple(sorted(index for _, index in indexed_centers[:split_index]))
    right = tuple(sorted(index for _, index in indexed_centers[split_index:]))
    if not left or not right or gaps[split_index - 1] <= 0:
        raise ValueError(f"{svg_path.name}: could not separate painted elements into two horizontal groups")
    return left, right


def center_in_text_cell(source: Bitmap, cell_width: int) -> Bitmap:
    bbox = source.ink_bbox
    if bbox is None:
        raise ValueError("cannot center an empty glyph")
    left, top, right, bottom = bbox
    ink_width = right - left
    if ink_width > cell_width:
        raise ValueError(f"glyph ink width {ink_width} exceeds cell width {cell_width}")
    target_left = (cell_width - ink_width) // 2
    canvas = [["0"] * cell_width for _ in range(source.height)]
    for y in range(top, bottom):
        for x in range(left, right):
            if source.rows[y][x] == "1":
                canvas[y][target_left + x - left] = "1"
    rows = tuple("".join(row) for row in canvas)
    return Bitmap(cell_width, source.height, rows, advance=cell_width)


def extract_carbon_glyph(svg_path: Path, size: int, elements: Sequence[int] | None = None) -> Bitmap:
    return center_in_text_cell(bitmap_from_svg(svg_path, size, elements), size // 2)


def punctuation_bitmap(char: str, size: int) -> Bitmap:
    cell_width = size // 2
    pixel_size = max(1, round(size * 0.625))
    baseline = round(size * 23 / 32)
    high_size = (cell_width * SUPERSAMPLE, size * SUPERSAMPLE)
    canvas = Image.new("L", high_size, 255)
    draw = ImageDraw.Draw(canvas)
    high_font = ImageFont.truetype(str(FONT_PATH), pixel_size * SUPERSAMPLE)
    advance = draw.textlength(char, font=high_font) / SUPERSAMPLE
    x = (cell_width - advance) * SUPERSAMPLE / 2
    draw.text((round(x), baseline * SUPERSAMPLE), char, font=high_font, fill=0, anchor="ls")
    reduced = canvas.resize((cell_width, size), Image.Resampling.LANCZOS)
    pixels = list(reduced.get_flattened_data())
    rows = tuple(
        "".join("1" if pixels[y * cell_width + x] < THRESHOLD else "0" for x in range(cell_width))
        for y in range(size)
    )
    # A visible all-zero box for a space would be wasteful, but retaining the
    # exact cell makes all punctuation advances deterministic and monospaced.
    return Bitmap(cell_width, size, rows, advance=cell_width)


def packed_bytes(bitmap: Bitmap) -> bytes:
    output = bytearray()
    for row in bitmap.rows:
        for offset in range(0, bitmap.width, 8):
            value = 0
            for bit_index, bit in enumerate(row[offset : offset + 8]):
                if bit == "1":
                    value |= 0x80 >> bit_index
            output.append(value)
    return bytes(output)


def c_array(identifier: str, data: bytes) -> list[str]:
    lines = [f"static const uint8_t {identifier}[] = {{"]
    for offset in range(0, len(data), 12):
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in data[offset : offset + 12]) + ",")
    lines.append("};")
    return lines


def bitmap_json(bitmap: Bitmap) -> dict:
    return {
        "advance": bitmap.advance,
        "height": bitmap.height,
        "rows": list(bitmap.rows),
        "stride": bitmap.stride,
        "width": bitmap.width,
        "x_offset": bitmap.x_offset,
        "y_offset": bitmap.y_offset,
    }


def enum_for_char(char: str) -> str:
    if "A" <= char <= "Z":
        return f"UPPER_{char}"
    if "a" <= char <= "z":
        return f"LOWER_{char.upper()}"
    if "0" <= char <= "9":
        return f"DIGIT_{char}"
    raise ValueError(char)


def expected_source_catalog() -> list[dict]:
    sources: list[dict] = []
    for upper in string.ascii_uppercase:
        lower = upper.lower()
        filename = f"letter--{upper}{lower}.svg"
        sources.append(
            {
                "allowed_sizes": list(TEXT_SIZES),
                "kind": "letter_pair",
                "path": f"svg/32/{filename}",
                "status": "active",
                "typed_ids": [f"PJ_CARBON_GLYPH_UPPER_{upper}", f"PJ_CARBON_GLYPH_LOWER_{upper}"],
            }
        )
    for number in string.digits:
        sources.append(
            {
                "allowed_sizes": list(TEXT_SIZES),
                "glyph_extraction": {"codepoint": ord(number), "method": "complete_viewbox_centered_fixed_cell"},
                "kind": "number",
                "path": f"svg/32/number--{number}.svg",
                "status": "active",
                "typed_ids": [f"PJ_CARBON_GLYPH_DIGIT_{number}"],
            }
        )
    for number in string.digits:
        sources.append(
            {
                "allowed_sizes": list(TEXT_SIZES),
                "glyph_extraction": {
                    "composition_only": True,
                    "method": "complete_viewbox_centered_fixed_cell",
                },
                "kind": "number_small",
                "path": f"svg/32/number--small--{number}.svg",
                "status": "active",
                "typed_ids": [f"PJ_CARBON_GLYPH_SMALL_DIGIT_{number}"],
            }
        )
    for typed_id, filename, sizes in SEMANTIC_ICONS:
        sources.append(
            {
                "allowed_sizes": list(sizes),
                "kind": "semantic_icon",
                "path": f"svg/32/{filename}",
                "status": "active",
                "typed_ids": [f"PJ_CARBON_ICON_{typed_id}"],
            }
        )
    for filename in REFERENCE_FILES:
        sources.append(
            {
                "allowed_sizes": [],
                "kind": "reference_icon",
                "path": f"svg/32/{filename}",
                "status": "reference",
                "typed_ids": [],
            }
        )
    return sorted(sources, key=lambda source: source["path"])


def build_manifest() -> dict:
    sources = expected_source_catalog()
    for source in sources:
        path = CARBON_DIR / source["path"]
        if not path.is_file():
            raise ValueError(f"missing source {path.relative_to(ROOT)}")
        root = ET.fromstring(path.read_bytes())
        viewbox = tuple(float(value) for value in root.attrib.get("viewBox", "").replace(",", " ").split())
        if viewbox != (0.0, 0.0, 32.0, 32.0):
            raise ValueError(f"{path.name}: expected the complete Carbon 0 0 32 32 viewbox")
        source["sha256"] = sha256_file(path)
        if source["kind"] == "letter_pair":
            upper = Path(source["path"]).stem.removeprefix("letter--")[0]
            lower = upper.lower()
            left, right = compute_letter_groups(path)
            source["glyph_extraction"] = {
                "cell": "half_viewbox_centered",
                "groups": [
                    {"codepoint": ord(upper), "elements": list(left), "typed_id": source["typed_ids"][0]},
                    {"codepoint": ord(lower), "elements": list(right), "typed_id": source["typed_ids"][1]},
                ],
                "method": "painted_element_horizontal_clustering",
                "split_at_x_16": False,
            }
    return {
        "derived": {
            "carbon_glyph_count": 72,
            "settings_composites": [
                {
                    "allowed_sizes": [64],
                    "components": ["PJ_CARBON_GLYPH_SMALL_DIGIT_1", "PJ_CARBON_GLYPH_SMALL_DIGIT_2", "PJ_CARBON_GLYPH_LOWER_H"],
                    "component_sizes": [64, 64, 32],
                    "typed_id": "PJ_CARBON_GLYPH_SETTINGS_12H",
                },
                {
                    "allowed_sizes": [64],
                    "components": ["PJ_CARBON_GLYPH_SMALL_DIGIT_2", "PJ_CARBON_GLYPH_SMALL_DIGIT_4", "PJ_CARBON_GLYPH_LOWER_H"],
                    "component_sizes": [64, 64, 32],
                    "typed_id": "PJ_CARBON_GLYPH_SETTINGS_24H",
                },
            ],
        },
        "font_fallback": {
            "family": "IBM Plex Mono Bold",
            "file": "assets/fonts/IBMPlexMono-Bold.ttf",
            "file_sha256": FONT_SHA256,
            "license": "SIL Open Font License 1.1",
            "license_file": "assets/fonts/IBMPlexMono-LICENSE.txt",
            "license_sha256": sha256_file(FONT_LICENSE_PATH),
            "roles": ["space", "ASCII punctuation", "unsupported-codepoint fallback"],
            "sizes": list(TEXT_SIZES),
            "version": FONT_VERSION,
        },
        "package": {
            "license": "Apache-2.0",
            "license_file": "assets/icons/carbon/LICENSE",
            "license_sha256": sha256_file(CARBON_DIR / "LICENSE"),
            "name": PACKAGE_NAME,
            "source_url": PACKAGE_URL,
            "tarball_sha1": PACKAGE_TARBALL_SHA1,
            "version": PACKAGE_VERSION,
        },
        "rasterization": {
            "cairosvg": CAIROSVG_VERSION,
            "complete_viewbox": [0, 0, 32, 32],
            "dither": False,
            "downsample": "Pillow.Resampling.LANCZOS",
            "pillow": PILLOW_VERSION,
            "supersample": SUPERSAMPLE,
            "threshold": THRESHOLD,
        },
        "schema_version": 1,
        "source_counts": {"active": 73, "reference": 26, "total": 99},
        "sources": sources,
    }


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def load_and_validate_manifest() -> dict:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    rebuilt = build_manifest()
    if manifest != rebuilt:
        raise ValueError("manifest does not match the checked-in sources; run --write-manifest after reviewing source changes")
    active = [source for source in manifest["sources"] if source["status"] == "active"]
    reference = [source for source in manifest["sources"] if source["status"] == "reference"]
    actual_files = sorted(str(path.relative_to(CARBON_DIR)) for path in SOURCE_DIR.glob("*.svg"))
    listed_files = sorted(source["path"] for source in manifest["sources"])
    if actual_files != listed_files:
        raise ValueError("manifest must list exactly every vendored Carbon SVG")
    if (len(active), len(reference), len(actual_files)) != (73, 26, 99):
        raise ValueError("Carbon source counts must be active=73, reference=26, total=99")
    semantic_records = sum(
        len(source["allowed_sizes"]) for source in active if source["kind"] == "semantic_icon"
    )
    if semantic_records != 30:
        raise ValueError(f"expected 30 compiled semantic records, found {semantic_records}")
    if cairosvg.__version__ != CAIROSVG_VERSION:
        raise ValueError(f"CairoSVG {CAIROSVG_VERSION} required, found {cairosvg.__version__}")
    import PIL

    if PIL.__version__ != PILLOW_VERSION:
        raise ValueError(f"Pillow {PILLOW_VERSION} required, found {PIL.__version__}")
    if sha256_file(FONT_PATH) != FONT_SHA256:
        raise ValueError("IBM Plex Mono Bold source hash differs from the approved font")
    if not FONT_LICENSE_PATH.is_file():
        raise ValueError("IBM Plex Mono license is missing")
    return manifest


def source_by_kind(manifest: dict, kind: str) -> list[dict]:
    return [source for source in manifest["sources"] if source["kind"] == kind]


def generate_icon_records(manifest: dict) -> list[dict]:
    records: list[dict] = []
    for source in source_by_kind(manifest, "semantic_icon"):
        typed_id = source["typed_ids"][0]
        for size in source["allowed_sizes"]:
            bitmap = bitmap_from_svg(CARBON_DIR / source["path"], size)
            records.append({"id": typed_id, "size": size, "source": source["path"], "bitmap": bitmap})
    return records


def generate_carbon_glyph_records(manifest: dict) -> list[dict]:
    records: list[dict] = []
    for source in source_by_kind(manifest, "letter_pair"):
        for group in source["glyph_extraction"]["groups"]:
            for size in source["allowed_sizes"]:
                bitmap = extract_carbon_glyph(CARBON_DIR / source["path"], size, group["elements"])
                records.append(
                    {
                        "codepoint": group["codepoint"],
                        "id": group["typed_id"],
                        "kind": "letter",
                        "size": size,
                        "source": source["path"],
                        "bitmap": bitmap,
                    }
                )
    for kind, id_prefix in (("number", "PJ_CARBON_GLYPH_DIGIT_"), ("number_small", "PJ_CARBON_GLYPH_SMALL_DIGIT_")):
        for source in source_by_kind(manifest, kind):
            digit = Path(source["path"]).stem[-1]
            codepoint = ord(digit) if kind == "number" else None
            for size in source["allowed_sizes"]:
                records.append(
                    {
                        "codepoint": codepoint,
                        "id": f"{id_prefix}{digit}",
                        "kind": kind,
                        "size": size,
                        "source": source["path"],
                        "bitmap": extract_carbon_glyph(CARBON_DIR / source["path"], size),
                    }
                )
    return records


def crop_ink(bitmap: Bitmap) -> Bitmap:
    bbox = bitmap.ink_bbox
    if bbox is None:
        raise ValueError("cannot crop empty bitmap")
    left, top, right, bottom = bbox
    rows = tuple(row[left:right] for row in bitmap.rows[top:bottom])
    return Bitmap(right - left, bottom - top, rows, advance=right - left)


def compose_settings_bitmap(component_bitmaps: Sequence[Bitmap]) -> Bitmap:
    cropped = [crop_ink(bitmap) for bitmap in component_bitmaps]
    gap = 4
    content_width = sum(bitmap.width for bitmap in cropped) + gap * (len(cropped) - 1)
    content_height = max(bitmap.height for bitmap in cropped)
    if content_width > 64 or content_height > 64:
        raise ValueError("Settings time-format composite exceeds its 64px launcher cell")
    canvas = [["0"] * 64 for _ in range(64)]
    cursor = (64 - content_width) // 2
    for bitmap in cropped:
        top = (64 - bitmap.height) // 2
        for y, row in enumerate(bitmap.rows):
            for x, bit in enumerate(row):
                if bit == "1":
                    canvas[top + y][cursor + x] = "1"
        cursor += bitmap.width + gap
    return Bitmap(64, 64, tuple("".join(row) for row in canvas), advance=64)


def generate_composites(glyph_records: list[dict]) -> list[dict]:
    by_key = {(record["id"], record["size"]): record["bitmap"] for record in glyph_records}
    definitions = (
        (
            "PJ_CARBON_GLYPH_SETTINGS_12H",
            (
                ("PJ_CARBON_GLYPH_SMALL_DIGIT_1", 64),
                ("PJ_CARBON_GLYPH_SMALL_DIGIT_2", 64),
                ("PJ_CARBON_GLYPH_LOWER_H", 32),
            ),
        ),
        (
            "PJ_CARBON_GLYPH_SETTINGS_24H",
            (
                ("PJ_CARBON_GLYPH_SMALL_DIGIT_2", 64),
                ("PJ_CARBON_GLYPH_SMALL_DIGIT_4", 64),
                ("PJ_CARBON_GLYPH_LOWER_H", 32),
            ),
        ),
    )
    return [
        {
            "codepoint": None,
            "id": typed_id,
            "kind": "settings_composite",
            "size": 64,
            "source": "+".join(f"{component}@{size}" for component, size in components),
            "bitmap": compose_settings_bitmap([by_key[(component, size)] for component, size in components]),
        }
        for typed_id, components in definitions
    ]


def generate_punctuation_records() -> list[dict]:
    return [
        {"codepoint": ord(char), "char": char, "size": size, "bitmap": punctuation_bitmap(char, size)}
        for char in PUNCTUATION
        for size in TEXT_SIZES
    ]


def generate_common_header() -> bytes:
    return b"""#pragma once

#include <stddef.h>
#include <stdint.h>

#define PJ_ASSET_ENCODING_ROW_MAJOR_MSB_FIRST 1

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    const uint8_t *data;
} pj_asset_bitmap_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t advance;
    int16_t x_offset;
    int16_t y_offset;
    const uint8_t *data;
} pj_asset_glyph_t;
"""


def short_id(typed_id: str, prefix: str) -> str:
    if not typed_id.startswith(prefix):
        raise ValueError(typed_id)
    return typed_id[len(prefix) :]


def generate_icon_header(records: list[dict]) -> bytes:
    unique_ids = [f"PJ_CARBON_ICON_{typed_id}" for typed_id, _, _ in SEMANTIC_ICONS]
    lines = [
        "#pragma once",
        "",
        '#include "pj_asset_bitmap.h"',
        "",
        "typedef enum {",
        "    PJ_CARBON_ICON_INVALID = -1,",
    ]
    for index, typed_id in enumerate(unique_ids):
        lines.append(f"    {typed_id} = {index},")
    lines += [f"    PJ_CARBON_ICON_ID_COUNT = {len(unique_ids)}", "} pj_carbon_icon_id_t;", ""]
    for record in records:
        suffix = short_id(record["id"], "PJ_CARBON_ICON_").lower()
        identifier = f"pj_carbon_icon_{suffix}_{record['size']}_data"
        record["c_data"] = identifier
        lines.extend(c_array(identifier, packed_bytes(record["bitmap"])))
        lines.append("")
    lines += [
        "typedef struct {",
        "    pj_carbon_icon_id_t id;",
        "    uint16_t size;",
        "    pj_asset_bitmap_t bitmap;",
        "} pj_carbon_icon_record_t;",
        "",
        f"#define PJ_CARBON_ICON_BITMAP_COUNT {len(records)}",
        "",
        "static const pj_carbon_icon_record_t PJ_CARBON_ICON_BITMAPS[PJ_CARBON_ICON_BITMAP_COUNT] = {",
    ]
    for record in records:
        bitmap = record["bitmap"]
        lines.append(
            f"    {{{record['id']}, {record['size']}, "
            f"{{{bitmap.width}, {bitmap.height}, {bitmap.stride}, {record['c_data']}}}}},"
        )
    lines += [
        "};",
        "",
        "static inline const pj_asset_bitmap_t *pj_carbon_icon_lookup(pj_carbon_icon_id_t id, uint16_t size)",
        "{",
        "    for (size_t index = 0; index < PJ_CARBON_ICON_BITMAP_COUNT; index++) {",
        "        const pj_carbon_icon_record_t *record = &PJ_CARBON_ICON_BITMAPS[index];",
        "        if (record->id == id && record->size == size) {",
        "            return &record->bitmap;",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
    ]
    return ("\n".join(lines)).encode("utf-8")


def carbon_glyph_ids() -> list[str]:
    return (
        [f"PJ_CARBON_GLYPH_UPPER_{char}" for char in string.ascii_uppercase]
        + [f"PJ_CARBON_GLYPH_LOWER_{char}" for char in string.ascii_uppercase]
        + [f"PJ_CARBON_GLYPH_DIGIT_{digit}" for digit in string.digits]
        + [f"PJ_CARBON_GLYPH_SMALL_DIGIT_{digit}" for digit in string.digits]
        + ["PJ_CARBON_GLYPH_SETTINGS_12H", "PJ_CARBON_GLYPH_SETTINGS_24H"]
    )


def generate_glyph_header(records: list[dict]) -> bytes:
    ids = carbon_glyph_ids()
    lines = [
        "#pragma once",
        "",
        '#include "pj_asset_bitmap.h"',
        "",
        "typedef enum {",
        "    PJ_CARBON_GLYPH_INVALID = -1,",
    ]
    for index, typed_id in enumerate(ids):
        lines.append(f"    {typed_id} = {index},")
    lines += [f"    PJ_CARBON_GLYPH_ID_COUNT = {len(ids)}", "} pj_carbon_glyph_id_t;", ""]
    for record in records:
        suffix = short_id(record["id"], "PJ_CARBON_GLYPH_").lower()
        identifier = f"pj_carbon_glyph_{suffix}_{record['size']}_data"
        record["c_data"] = identifier
        lines.extend(c_array(identifier, packed_bytes(record["bitmap"])))
        lines.append("")
    lines += [
        "typedef struct {",
        "    pj_carbon_glyph_id_t id;",
        "    uint32_t codepoint;",
        "    uint16_t size;",
        "    pj_asset_glyph_t glyph;",
        "} pj_carbon_glyph_record_t;",
        "",
        f"#define PJ_CARBON_GLYPH_BITMAP_COUNT {len(records)}",
        "#define PJ_CARBON_DERIVED_GLYPH_COUNT 72",
        "",
        "static const pj_carbon_glyph_record_t PJ_CARBON_GLYPH_BITMAPS[PJ_CARBON_GLYPH_BITMAP_COUNT] = {",
    ]
    for record in records:
        bitmap = record["bitmap"]
        codepoint = record["codepoint"] or 0
        lines.append(
            f"    {{{record['id']}, {codepoint}, {record['size']}, "
            f"{{{bitmap.width}, {bitmap.height}, {bitmap.stride}, {bitmap.advance}, "
            f"{bitmap.x_offset}, {bitmap.y_offset}, {record['c_data']}}}}},"
        )
    lines += [
        "};",
        "",
        "static inline pj_carbon_glyph_id_t pj_carbon_glyph_id_for_codepoint(uint32_t codepoint)",
        "{",
        "    if (codepoint >= 'A' && codepoint <= 'Z') {",
        "        return (pj_carbon_glyph_id_t)(PJ_CARBON_GLYPH_UPPER_A + codepoint - 'A');",
        "    }",
        "    if (codepoint >= 'a' && codepoint <= 'z') {",
        "        return (pj_carbon_glyph_id_t)(PJ_CARBON_GLYPH_LOWER_A + codepoint - 'a');",
        "    }",
        "    if (codepoint >= '0' && codepoint <= '9') {",
        "        return (pj_carbon_glyph_id_t)(PJ_CARBON_GLYPH_DIGIT_0 + codepoint - '0');",
        "    }",
        "    return PJ_CARBON_GLYPH_INVALID;",
        "}",
        "",
        "static inline const pj_asset_glyph_t *pj_carbon_glyph_lookup(pj_carbon_glyph_id_t id, uint16_t size)",
        "{",
        "    for (size_t index = 0; index < PJ_CARBON_GLYPH_BITMAP_COUNT; index++) {",
        "        const pj_carbon_glyph_record_t *record = &PJ_CARBON_GLYPH_BITMAPS[index];",
        "        if (record->id == id && record->size == size) {",
        "            return &record->glyph;",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
        "static inline const pj_asset_glyph_t *pj_carbon_glyph_lookup_codepoint(uint32_t codepoint, uint16_t size)",
        "{",
        "    return pj_carbon_glyph_lookup(pj_carbon_glyph_id_for_codepoint(codepoint), size);",
        "}",
        "",
    ]
    return ("\n".join(lines)).encode("utf-8")


def generate_punctuation_header(records: list[dict]) -> bytes:
    lines = [
        "#pragma once",
        "",
        '#include "pj_asset_bitmap.h"',
        "",
        "#define PJ_IBM_PLEX_MONO_BOLD_FALLBACK_CODEPOINT '?'",
        f"#define PJ_IBM_PLEX_MONO_BOLD_PUNCTUATION_BITMAP_COUNT {len(records)}",
        "",
    ]
    for record in records:
        identifier = f"pj_ibm_plex_punct_{record['codepoint']}_{record['size']}_data"
        record["c_data"] = identifier
        lines.extend(c_array(identifier, packed_bytes(record["bitmap"])))
        lines.append("")
    lines += [
        "typedef struct {",
        "    uint32_t codepoint;",
        "    uint16_t size;",
        "    pj_asset_glyph_t glyph;",
        "} pj_ibm_plex_punctuation_record_t;",
        "",
        "static const pj_ibm_plex_punctuation_record_t PJ_IBM_PLEX_MONO_BOLD_PUNCTUATION_BITMAPS[PJ_IBM_PLEX_MONO_BOLD_PUNCTUATION_BITMAP_COUNT] = {",
    ]
    for record in records:
        bitmap = record["bitmap"]
        lines.append(
            f"    {{{record['codepoint']}, {record['size']}, "
            f"{{{bitmap.width}, {bitmap.height}, {bitmap.stride}, {bitmap.advance}, "
            f"{bitmap.x_offset}, {bitmap.y_offset}, {record['c_data']}}}}},"
        )
    lines += [
        "};",
        "",
        "static inline const pj_asset_glyph_t *pj_ibm_plex_punctuation_lookup(uint32_t codepoint, uint16_t size)",
        "{",
        "    const pj_asset_glyph_t *fallback = NULL;",
        "    for (size_t index = 0; index < PJ_IBM_PLEX_MONO_BOLD_PUNCTUATION_BITMAP_COUNT; index++) {",
        "        const pj_ibm_plex_punctuation_record_t *record = &PJ_IBM_PLEX_MONO_BOLD_PUNCTUATION_BITMAPS[index];",
        "        if (record->codepoint == PJ_IBM_PLEX_MONO_BOLD_FALLBACK_CODEPOINT && record->size == size) {",
        "            fallback = &record->glyph;",
        "        }",
        "        if (record->codepoint == codepoint && record->size == size) {",
        "            return &record->glyph;",
        "        }",
        "    }",
        "    return fallback;",
        "}",
        "",
    ]
    return ("\n".join(lines)).encode("utf-8")


def generate_sim_outputs(manifest: dict, icons: list[dict], glyphs: list[dict], punctuation: list[dict]) -> dict[Path, bytes]:
    metadata = {
        "encoding": "1bit-row-major-msb-first",
        "generator": "tools/generate_icon_assets.py",
        "package": {"name": PACKAGE_NAME, "version": PACKAGE_VERSION},
        "rasterization": manifest["rasterization"],
        "schema_version": 1,
    }
    icon_output = {
        **metadata,
        "family": "Carbon Icons",
        "record_count": len(icons),
        "records": [
            {
                "id": record["id"],
                "size": record["size"],
                "source": f"assets/icons/carbon/{record['source']}",
                **bitmap_json(record["bitmap"]),
            }
            for record in icons
        ],
    }
    glyph_output = {
        **metadata,
        "derived_identity_count": 72,
        "family": "Carbon Icons",
        "records": [
            {
                "codepoint": record["codepoint"],
                "id": record["id"],
                "kind": record["kind"],
                "size": record["size"],
                "source": record["source"],
                **bitmap_json(record["bitmap"]),
            }
            for record in glyphs
        ],
    }
    punctuation_output = {
        "encoding": "1bit-row-major-msb-first",
        "fallback_codepoint": ord("?"),
        "family": "IBM Plex Mono Bold",
        "font_sha256": FONT_SHA256,
        "font_version": FONT_VERSION,
        "generator": "tools/generate_icon_assets.py",
        "license": "SIL Open Font License 1.1",
        "record_count": len(punctuation),
        "records": [
            {
                "char": record["char"],
                "codepoint": record["codepoint"],
                "size": record["size"],
                **bitmap_json(record["bitmap"]),
            }
            for record in punctuation
        ],
        "roles": ["space", "ASCII punctuation", "unsupported-codepoint fallback"],
        "schema_version": 1,
    }
    return {
        SIM_ICON_OUTPUT: canonical_json(icon_output),
        SIM_GLYPH_OUTPUT: canonical_json(glyph_output),
        SIM_PUNCT_OUTPUT: canonical_json(punctuation_output),
    }


def canvas(record: dict, theme: str) -> str:
    bitmap = record["bitmap"]
    rows = "/".join(bitmap.rows)
    return (
        f'<canvas class="pixel {theme}" width="{bitmap.width}" height="{bitmap.height}" '
        f'data-rows="{rows}" aria-label="{html.escape(record["id"])} {record["size"]} {theme}"></canvas>'
    )


def source_card(source: dict) -> str:
    labels = ", ".join(source["typed_ids"]) or "reference only — no runtime ID"
    return (
        '<article class="source-card">'
        f'<img src="{html.escape(source["path"])}" alt="">'
        f'<strong>{html.escape(Path(source["path"]).name)}</strong>'
        f'<code>{html.escape(labels)}</code>'
        f'<small>{source["status"]} · SHA-256 {source["sha256"][:12]}…</small>'
        "</article>"
    )


def bitmap_card(record: dict) -> str:
    return (
        '<article class="bitmap-card">'
        '<div class="themes">'
        + canvas(record, "light")
        + canvas(record, "dark")
        + "</div>"
        + f'<strong>{html.escape(record["id"])}</strong>'
        + f'<code>{record["bitmap"].width}×{record["bitmap"].height} · exact size {record["size"]}</code>'
        + f'<small>{html.escape(record["source"])}</small>'
        + "</article>"
    )


def generate_gallery(manifest: dict, icons: list[dict], glyphs: list[dict]) -> bytes:
    active = [source for source in manifest["sources"] if source["status"] == "active"]
    references = [source for source in manifest["sources"] if source["status"] == "reference"]
    preview_glyphs = [record for record in glyphs if record["size"] == 32 and record["kind"] in {"letter", "number", "number_small"}]
    composites = [record for record in glyphs if record["kind"] == "settings_composite"]
    body = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Pocket Journal Carbon asset contract</title>
  <style>
    :root {{ color-scheme: light dark; --bg:#f4f4f4; --panel:#fff; --ink:#161616; --muted:#525252; --line:#c6c6c6; }}
    * {{ box-sizing:border-box; }}
    body {{ margin:0; background:var(--bg); color:var(--ink); font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace; }}
    header {{ padding:24px; border-bottom:4px solid var(--ink); background:var(--panel); }}
    h1,h2,p {{ margin:0; }} h2 {{ margin:32px 0 12px; }} p {{ margin-top:8px; max-width:92ch; color:var(--muted); }}
    main {{ padding:0 24px 48px; }} a {{ color:inherit; }}
    .grid {{ display:grid; grid-template-columns:repeat(auto-fill,minmax(220px,1fr)); gap:12px; }}
    .bitmap-card,.source-card {{ min-width:0; padding:12px; border:1px solid var(--line); background:var(--panel); display:grid; gap:8px; align-content:start; }}
    .themes {{ display:grid; grid-template-columns:1fr 1fr; gap:8px; }}
    canvas.pixel {{ width:100%; height:112px; image-rendering:pixelated; object-fit:contain; border:1px solid var(--line); }}
    canvas.light {{ background:#fff; }} canvas.dark {{ background:#161616; }}
    .source-card img {{ width:64px; height:64px; }} code,small,strong {{ overflow-wrap:anywhere; }} small {{ color:var(--muted); }}
    @media (prefers-color-scheme:dark) {{ :root {{ --bg:#161616; --panel:#262626; --ink:#f4f4f4; --muted:#c6c6c6; --line:#525252; }} .source-card img {{ filter:invert(1); }} }}
  </style>
</head>
<body>
<header>
  <h1>Deterministic Carbon 1-bit asset contract</h1>
  <p>Official <a href="{LIBRARY_URL}">Carbon icon library</a>, package {PACKAGE_NAME}@{PACKAGE_VERSION}. Complete 32×32 viewboxes; CairoSVG {CAIROSVG_VERSION} at {SUPERSAMPLE}×; fixed Lanczos; threshold {THRESHOLD}; no dithering. The four legacy Arrow directions are reference-only.</p>
</header>
<main>
  <h2>Compiled semantic records ({len(icons)})</h2>
  <div class="grid">{''.join(bitmap_card(record) for record in icons)}</div>
  <h2>Derived letters ({sum(1 for r in preview_glyphs if r['kind'] == 'letter')})</h2>
  <p>Uppercase and lowercase are extracted from painted-element horizontal groups, never by cutting at x=16.</p>
  <div class="grid">{''.join(bitmap_card(record) for record in preview_glyphs if record['kind'] == 'letter')}</div>
  <h2>Digit variants ({sum(1 for r in preview_glyphs if r['kind'] in {'number', 'number_small'})})</h2>
  <div class="grid">{''.join(bitmap_card(record) for record in preview_glyphs if record['kind'] in {'number', 'number_small'})}</div>
  <h2>Settings composites (2)</h2>
  <div class="grid">{''.join(bitmap_card(record) for record in composites)}</div>
  <h2>Active upstream sources ({len(active)})</h2>
  <div class="grid">{''.join(source_card(source) for source in active)}</div>
  <h2>Reference-only upstream sources ({len(references)})</h2>
  <div class="grid">{''.join(source_card(source) for source in references)}</div>
</main>
<script>
  for (const canvas of document.querySelectorAll("canvas[data-rows]")) {{
    const rows = canvas.dataset.rows.split("/");
    const context = canvas.getContext("2d");
    const dark = canvas.classList.contains("dark");
    context.fillStyle = dark ? "#161616" : "#ffffff";
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = dark ? "#ffffff" : "#161616";
    rows.forEach((row, y) => [...row].forEach((bit, x) => {{ if (bit === "1") context.fillRect(x, y, 1, 1); }}));
  }}
</script>
</body>
</html>
"""
    return body.encode("utf-8")


def build_outputs(manifest: dict) -> dict[Path, bytes]:
    icons = generate_icon_records(manifest)
    base_glyphs = generate_carbon_glyph_records(manifest)
    glyphs = base_glyphs + generate_composites(base_glyphs)
    punctuation = generate_punctuation_records()
    outputs = {
        COMMON_HEADER: generate_common_header(),
        ICON_HEADER: generate_icon_header(icons),
        GLYPH_HEADER: generate_glyph_header(glyphs),
        PUNCT_HEADER: generate_punctuation_header(punctuation),
        GALLERY_PATH: generate_gallery(manifest, icons, glyphs),
    }
    outputs.update(generate_sim_outputs(manifest, icons, glyphs, punctuation))
    return outputs


def write_outputs(outputs: dict[Path, bytes]) -> None:
    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def check_outputs(outputs: dict[Path, bytes]) -> list[str]:
    mismatches: list[str] = []
    for path, content in outputs.items():
        if not path.is_file():
            mismatches.append(f"missing {path.relative_to(ROOT)}")
        elif path.read_bytes() != content:
            mismatches.append(f"stale {path.relative_to(ROOT)}")
    return mismatches


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify sources and byte-identical generated outputs")
    parser.add_argument(
        "--write-manifest",
        action="store_true",
        help="rewrite source hashes/extraction groups after manually reviewing vendored SVG changes",
    )
    args = parser.parse_args(argv)

    if args.check and args.write_manifest:
        parser.error("--check and --write-manifest are mutually exclusive")
    if args.write_manifest:
        MANIFEST_PATH.write_bytes(canonical_json(build_manifest()))
        print(f"wrote {MANIFEST_PATH.relative_to(ROOT)}")
        return 0

    try:
        manifest = load_and_validate_manifest()
        first = build_outputs(manifest)
        # A second in-process generation catches accidental generator state and
        # is also exercised by --check, not merely by the unit tests.
        second = build_outputs(manifest)
        if first != second:
            raise ValueError("asset generation is not byte-identical across two runs")
    except (OSError, ValueError) as error:
        print(f"asset generation failed: {error}", file=sys.stderr)
        return 1

    if args.check:
        mismatches = check_outputs(first)
        if mismatches:
            for mismatch in mismatches:
                print(mismatch, file=sys.stderr)
            return 1
        print("Carbon assets verified: 73 active + 26 reference sources, 72 derived glyphs, 30 semantic records")
        return 0

    write_outputs(first)
    print("generated deterministic Carbon firmware, simulator, punctuation, and gallery assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
