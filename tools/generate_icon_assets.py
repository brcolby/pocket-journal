#!/usr/bin/env python3
"""Generate 1-bit Carbon icon assets for firmware and simulator."""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "assets/icons/carbon/svg/32"
FIRMWARE_OUT = ROOT / "firmware/components/pj_ui/include/pj_icon_carbon.h"
SIM_OUT = ROOT / "simulator/assets/icons/carbon-1bit.json"

ICONS = {
    "add": "add.svg",
    "alarm": "alarm.svg",
    "document_audio": "document--audio.svg",
    "microphone": "microphone.svg",
    "notebook": "notebook.svg",
    "pause": "pause.svg",
    "play": "play.svg",
    "power": "power.svg",
    "read_me": "read-me.svg",
    "repeat": "repeat.svg",
    "reset": "reset.svg",
    "settings": "settings.svg",
    "settings_adjust": "settings--adjust.svg",
    "stop": "stop.svg",
    "subtract": "subtract.svg",
    "time": "time.svg",
    "timer": "timer.svg",
    "volume_up": "volume--up.svg",
    "wifi": "wifi.svg",
}

SIZES = [14, 18, 24, 32, 34, 38, 40, 58, 66, 82]


def render_svg(svg_path: Path, out_dir: Path, source_px: int = 256) -> Path:
    subprocess.run(
        ["qlmanage", "-t", "-s", str(source_px), "-o", str(out_dir), str(svg_path)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return out_dir / f"{svg_path.name}.png"


def rows_from_png(png_path: Path, size: int) -> list[str]:
    image = Image.open(png_path).convert("RGBA")
    alpha = image.getchannel("A")
    bbox = alpha.getbbox()
    if bbox is not None:
        image = image.crop(bbox)

    image.thumbnail((size, size), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    canvas.alpha_composite(image, ((size - image.width) // 2, (size - image.height) // 2))

    rows: list[str] = []
    for y in range(size):
        row = []
        for x in range(size):
            red, green, blue, alpha_value = canvas.getpixel((x, y))
            if alpha_value < 32:
                row.append("0")
                continue
            luminance = (red * 299 + green * 587 + blue * 114) // 1000
            row.append("1" if luminance < 192 else "0")
        rows.append("".join(row))
    return rows


def pack_rows(rows: list[str]) -> list[int]:
    packed: list[int] = []
    for row in rows:
        value = 0
        bit_count = 0
        for bit in row:
            value = (value << 1) | (1 if bit == "1" else 0)
            bit_count += 1
            if bit_count == 8:
                packed.append(value)
                value = 0
                bit_count = 0
        if bit_count:
            packed.append(value << (8 - bit_count))
    return packed


def c_array(name: str, data: list[int]) -> str:
    chunks = []
    for index in range(0, len(data), 12):
        chunks.append("    " + ", ".join(f"0x{byte:02x}" for byte in data[index : index + 12]))
    return f"static const uint8_t {name}[] = {{\n" + ",\n".join(chunks) + "\n};\n"


def make_c_identifier(icon_name: str, size: int) -> str:
    return f"pj_icon_{icon_name}_{size}"


def generate() -> None:
    if shutil.which("qlmanage") is None:
        raise SystemExit("qlmanage is required to render Carbon SVGs on macOS")

    missing = [filename for filename in ICONS.values() if not (SOURCE_DIR / filename).exists()]
    if missing:
        raise SystemExit(f"missing Carbon SVGs: {', '.join(missing)}")

    SIM_OUT.parent.mkdir(parents=True, exist_ok=True)
    FIRMWARE_OUT.parent.mkdir(parents=True, exist_ok=True)

    simulator = {
        "family": "Carbon Icons",
        "source": "assets/icons/carbon/svg/32",
        "sizes": SIZES,
        "icons": {},
    }
    c_arrays: list[str] = []
    c_records: list[str] = []

    with tempfile.TemporaryDirectory(prefix="pj-carbon-icons-") as tmp:
        tmp_path = Path(tmp)
        for icon_name, filename in ICONS.items():
            png_path = render_svg(SOURCE_DIR / filename, tmp_path)
            simulator["icons"][icon_name] = {}
            for size in SIZES:
                rows = rows_from_png(png_path, size)
                packed = pack_rows(rows)
                identifier = make_c_identifier(icon_name, size)
                stride = (size + 7) // 8
                simulator["icons"][icon_name][str(size)] = {"width": size, "height": size, "rows": rows}
                c_arrays.append(c_array(identifier, packed))
                c_records.append(
                    f'    {{"{icon_name}", {size}, {size}, {stride}, {identifier}}},'
                )

    firmware = """#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint8_t width;
    uint8_t height;
    uint8_t stride;
    const uint8_t *data;
} pj_icon_bitmap_t;

"""
    firmware += "\n".join(c_arrays)
    firmware += "\nstatic const pj_icon_bitmap_t PJ_CARBON_ICONS[] = {\n"
    firmware += "\n".join(c_records)
    firmware += "\n};\n\n"
    firmware += "static const size_t PJ_CARBON_ICON_COUNT = sizeof(PJ_CARBON_ICONS) / sizeof(PJ_CARBON_ICONS[0]);\n"

    FIRMWARE_OUT.write_text(firmware, encoding="utf-8")
    SIM_OUT.write_text(json.dumps(simulator, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate()
