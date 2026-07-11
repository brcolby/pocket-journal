#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import json

from PIL import Image, ImageDraw, ImageFont


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FONT = REPO_ROOT / "assets/fonts/IBMPlexMono-Bold.ttf"
SIM_OUTPUT = REPO_ROOT / "simulator/assets/fonts/space-mono-bold-1bit.json"
FW_OUTPUT = REPO_ROOT / "firmware/components/pj_ui/include/pj_font_space_mono.h"
ASCII_CHARS = "".join(chr(code) for code in range(32, 127))
LOGICAL_SIZES = {
    1: 8,
    2: 14,
    3: 22,
    4: 30,
}
THRESHOLD = 220
Y_OFFSET_ADJUSTMENTS = {
    "+": {1: 1, 2: 2, 3: 3, 4: 5},
    "-": {1: 1, 2: 2, 3: 3, 4: 5},
    "/": {1: -1, 2: -2, 3: -3, 4: -4},
}


@dataclass
class Glyph:
    char: str
    width: int
    height: int
    advance: int
    x_offset: int
    y_offset: int
    rows: list[str]


def rasterize_glyph(font: ImageFont.FreeTypeFont, char: str) -> Glyph:
    bbox = font.getbbox(char, anchor="lt")
    advance = max(1, int(round(font.getlength(char))))
    width = max(1, bbox[2] - bbox[0])
    height = max(1, bbox[3] - bbox[1])
    image = Image.new("L", (width + 4, height + 4), 255)
    draw = ImageDraw.Draw(image)
    draw.text((2 - bbox[0], 2 - bbox[1]), char, font=font, fill=0, anchor="lt")
    ink_mask = image.point(lambda pixel: 255 if pixel < THRESHOLD else 0)
    bbox_image = ink_mask.getbbox()
    if bbox_image is None:
        return Glyph(char, max(1, advance // 2), 1, advance, 0, 0, ["0" * max(1, advance // 2)])

    cropped = image.crop(bbox_image)
    rows = []
    for y in range(cropped.height):
        row = []
        for x in range(cropped.width):
            # Threshold antialiasing to a crisp 1-bit glyph. No dithering.
            row.append("1" if cropped.getpixel((x, y)) < THRESHOLD else "0")
        rows.append("".join(row))
    return Glyph(
        char=char,
        width=cropped.width,
        height=cropped.height,
        advance=advance,
        x_offset=bbox_image[0] - 2 + bbox[0],
        y_offset=bbox_image[1] - 2 + bbox[1],
        rows=rows,
    )


def generate(font_path: Path) -> dict:
    sizes = {}
    for logical_size, pixel_size in LOGICAL_SIZES.items():
        font = ImageFont.truetype(str(font_path), pixel_size)
        metrics = font.getmetrics()
        glyphs = {}
        for char in ASCII_CHARS:
            glyph = rasterize_glyph(font, char)
            glyph.y_offset += Y_OFFSET_ADJUSTMENTS.get(char, {}).get(logical_size, 0)
            glyphs[char] = glyph.__dict__
        sizes[str(logical_size)] = {
            "pixel_size": pixel_size,
            "ascent": metrics[0],
            "descent": metrics[1],
            "line_height": metrics[0] + metrics[1],
            "glyphs": glyphs,
        }
    return {
        "family": "IBM Plex Mono Bold",
        "source": str(font_path.relative_to(REPO_ROOT)),
        "encoding": "1bit-rows",
        "sizes": sizes,
    }


def write_sim_asset(asset: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(asset, separators=(",", ":")) + "\n", encoding="utf-8")


def c_identifier(char: str) -> str:
    code = ord(char)
    return f"glyph_{code}"


def packed_bytes(rows: list[str]) -> list[int]:
    bits = "".join(rows)
    values = []
    for index in range(0, len(bits), 8):
        chunk = bits[index:index + 8].ljust(8, "0")
        value = 0
        for bit_index, bit in enumerate(chunk):
            if bit == "1":
                value |= 1 << bit_index
        values.append(value)
    return values


def write_fw_header(asset: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint8_t width;",
        "    uint8_t height;",
        "    uint8_t advance;",
        "    int8_t x_offset;",
        "    int8_t y_offset;",
        "    const uint8_t *data;",
        "} pj_font_glyph_t;",
        "",
        "typedef struct {",
        "    uint8_t logical_size;",
        "    uint8_t pixel_size;",
        "    uint8_t ascent;",
        "    uint8_t descent;",
        "    uint8_t line_height;",
        "    const pj_font_glyph_t *glyphs;",
        "} pj_font_size_t;",
        "",
        "#define PJ_FONT_SPACE_MONO_FIRST 32",
        "#define PJ_FONT_SPACE_MONO_LAST 126",
        "#define PJ_FONT_SPACE_MONO_GLYPH_COUNT 95",
        "#define PJ_FONT_SPACE_MONO_SIZE_COUNT 4",
        "",
    ]

    for size_key, size in asset["sizes"].items():
        for char, glyph in size["glyphs"].items():
            values = packed_bytes(glyph["rows"])
            lines.append(f"static const uint8_t pj_space_mono_{size_key}_{c_identifier(char)}[] = {{")
            lines.append("    " + ", ".join(f"0x{value:02x}" for value in values) + ",")
            lines.append("};")
        lines.append("")
        lines.append(f"static const pj_font_glyph_t pj_space_mono_{size_key}_glyphs[PJ_FONT_SPACE_MONO_GLYPH_COUNT] = {{")
        for char in ASCII_CHARS:
            glyph = size["glyphs"][char]
            lines.append(
                "    "
                f"{{{glyph['width']}, {glyph['height']}, {glyph['advance']}, "
                f"{glyph['x_offset']}, {glyph['y_offset']}, "
                f"pj_space_mono_{size_key}_{c_identifier(char)}}},"
            )
        lines.append("};")
        lines.append("")

    lines.append("static const pj_font_size_t PJ_FONT_SPACE_MONO[PJ_FONT_SPACE_MONO_SIZE_COUNT] = {")
    for size_key, size in asset["sizes"].items():
        lines.append(
            "    "
            f"{{{size_key}, {size['pixel_size']}, {size['ascent']}, {size['descent']}, "
            f"{size['line_height']}, pj_space_mono_{size_key}_glyphs}},"
        )
    lines.append("};")
    lines.append("")
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", default=str(DEFAULT_FONT))
    parser.add_argument("--sim-output", default=str(SIM_OUTPUT))
    parser.add_argument("--fw-output", default=str(FW_OUTPUT))
    args = parser.parse_args()

    asset = generate(Path(args.font))
    write_sim_asset(asset, Path(args.sim_output))
    write_fw_header(asset, Path(args.fw_output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
