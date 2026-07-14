#!/usr/bin/env python3
"""Generate Pocket Journal's compiled 1-bit default artwork."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "assets/static/pocket-journal-default.png"
DEFAULT_PBM_OUTPUT = ROOT / "assets/static/pocket-journal-default.pbm"
DEFAULT_HEADER_OUTPUT = ROOT / "firmware/components/pj_ui/include/pj_default_static_art.h"
DEFAULT_SOURCE_OUTPUT = ROOT / "firmware/components/pj_ui/pj_default_static_art.c"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
WIDTH = 200
HEIGHT = 200
THRESHOLD = 128


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def decode_png(path: Path) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path}: not a PNG file")

    offset = len(PNG_SIGNATURE)
    header: tuple[int, int, int, int, int, int, int] | None = None
    compressed = bytearray()
    saw_end = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_end = offset + 12 + length
        if chunk_end > len(data):
            raise ValueError(f"{path}: truncated {chunk_type.decode('ascii', 'replace')} chunk")
        chunk_data = data[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        actual_crc = zlib.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            raise ValueError(f"{path}: invalid {chunk_type.decode('ascii', 'replace')} CRC")

        if chunk_type == b"IHDR":
            if header is not None or length != 13:
                raise ValueError(f"{path}: invalid IHDR chunk")
            header = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            if length != 0:
                raise ValueError(f"{path}: invalid IEND chunk")
            saw_end = True
            offset = chunk_end
            break
        elif not (chunk_type[0] & 0x20):
            raise ValueError(f"{path}: unsupported critical PNG chunk {chunk_type!r}")
        offset = chunk_end

    if header is None or not saw_end or offset != len(data):
        raise ValueError(f"{path}: incomplete PNG structure")
    width, height, bit_depth, color_type, compression, filter_method, interlace = header
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: expected {WIDTH}x{HEIGHT}, got {width}x{height}")
    if (bit_depth, color_type) != (8, 0):
        raise ValueError(
            f"{path}: expected 8-bit grayscale PNG without alpha, "
            f"got bit depth {bit_depth}, color type {color_type}"
        )
    if (compression, filter_method, interlace) != (0, 0, 0):
        raise ValueError(f"{path}: unsupported PNG compression, filter, or interlace mode")
    if not compressed:
        raise ValueError(f"{path}: PNG has no image data")

    raw = zlib.decompress(bytes(compressed))
    stride = width
    expected_size = height * (stride + 1)
    if len(raw) != expected_size:
        raise ValueError(f"{path}: expected {expected_size} decoded bytes, got {len(raw)}")

    rows: list[bytes] = []
    previous = bytes(stride)
    for y in range(height):
        row_offset = y * (stride + 1)
        filter_type = raw[row_offset]
        encoded = raw[row_offset + 1 : row_offset + 1 + stride]
        decoded = bytearray(stride)
        for x, value in enumerate(encoded):
            left = decoded[x - 1] if x else 0
            above = previous[x]
            upper_left = previous[x - 1] if x else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = paeth(left, above, upper_left)
            else:
                raise ValueError(f"{path}: unsupported PNG filter {filter_type} on row {y}")
            decoded[x] = (value + predictor) & 0xFF
        rows.append(bytes(decoded))
        previous = rows[-1]
    return width, height, rows


def monochrome_rows(grayscale_rows: list[bytes]) -> list[str]:
    return ["".join("1" if value < THRESHOLD else "0" for value in row) for row in grayscale_rows]


def pack_rows(rows: list[str]) -> bytes:
    packed = bytearray((WIDTH * HEIGHT) // 8)
    for y, row in enumerate(rows):
        if len(row) != WIDTH:
            raise ValueError(f"row {y} has {len(row)} pixels, expected {WIDTH}")
        for x, pixel in enumerate(row):
            if pixel == "1":
                index = y * WIDTH + x
                packed[index >> 3] |= 1 << (index & 7)
            elif pixel != "0":
                raise ValueError(f"row {y} contains invalid pixel {pixel!r}")
    return bytes(packed)


def source_label(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def render_header(source_sha: str, packed_sha: str, black_pixels: int) -> str:
    return f"""/* Generated by tools/generate_static_art.py; do not edit. */
#pragma once

#include <stdint.h>

#define PJ_DEFAULT_STATIC_ART_WIDTH {WIDTH}
#define PJ_DEFAULT_STATIC_ART_HEIGHT {HEIGHT}
#define PJ_DEFAULT_STATIC_ART_BYTES {WIDTH * HEIGHT // 8}
#define PJ_DEFAULT_STATIC_ART_THRESHOLD {THRESHOLD}
#define PJ_DEFAULT_STATIC_ART_BLACK_PIXELS {black_pixels}
#define PJ_DEFAULT_STATIC_ART_SOURCE_SHA256 \"{source_sha}\"
#define PJ_DEFAULT_STATIC_ART_PACKED_SHA256 \"{packed_sha}\"

extern const uint8_t pj_default_static_art[PJ_DEFAULT_STATIC_ART_BYTES];
"""


def render_c(source: Path, source_sha: str, packed: bytes) -> str:
    lines = [
        "/* Generated by tools/generate_static_art.py; do not edit.",
        f" * Source: {source_label(source)}",
        f" * Source SHA-256: {source_sha}",
        f" * Conversion: grayscale < {THRESHOLD} is black; row-major, LSB-first bits.",
        " */",
        '#include "pj_default_static_art.h"',
        "",
        "const uint8_t pj_default_static_art[PJ_DEFAULT_STATIC_ART_BYTES] = {",
    ]
    for offset in range(0, len(packed), 12):
        values = ", ".join(f"0x{value:02x}" for value in packed[offset : offset + 12])
        lines.append(f"    {values},")
    lines.extend(["};", ""])
    return "\n".join(lines)


def render_pbm(source: Path, source_sha: str, rows: list[str]) -> str:
    lines = [
        "P1",
        f"# Generated from {source_label(source)} by tools/generate_static_art.py",
        f"# Source SHA-256: {source_sha}; grayscale < {THRESHOLD} is black",
        f"{WIDTH} {HEIGHT}",
    ]
    lines.extend(" ".join(row) for row in rows)
    return "\n".join(lines) + "\n"


def generated_outputs(
    source: Path, pbm_output: Path, header_output: Path, c_output: Path
) -> tuple[dict[Path, bytes], int, str]:
    _, _, grayscale_rows = decode_png(source)
    rows = monochrome_rows(grayscale_rows)
    packed = pack_rows(rows)
    source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
    packed_sha = hashlib.sha256(packed).hexdigest()
    black_pixels = sum(row.count("1") for row in rows)
    outputs = {
        pbm_output: render_pbm(source, source_sha, rows).encode("ascii"),
        header_output: render_header(source_sha, packed_sha, black_pixels).encode("ascii"),
        c_output: render_c(source, source_sha, packed).encode("ascii"),
    }
    return outputs, black_pixels, packed_sha


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--pbm-output", type=Path, default=DEFAULT_PBM_OUTPUT)
    parser.add_argument("--header-output", type=Path, default=DEFAULT_HEADER_OUTPUT)
    parser.add_argument("--c-output", type=Path, default=DEFAULT_SOURCE_OUTPUT)
    parser.add_argument("--check", action="store_true", help="fail if committed outputs are stale")
    args = parser.parse_args()

    try:
        outputs, black_pixels, packed_sha = generated_outputs(
            args.source, args.pbm_output, args.header_output, args.c_output
        )
    except (OSError, ValueError, zlib.error) as error:
        print(error, file=sys.stderr)
        return 1

    if args.check:
        stale = []
        for path, expected in outputs.items():
            try:
                actual = path.read_bytes()
            except FileNotFoundError:
                stale.append(f"missing {path}")
                continue
            if actual != expected:
                stale.append(f"stale {path}")
        if stale:
            print("\n".join(stale), file=sys.stderr)
            print("run: make generate-static-art", file=sys.stderr)
            return 1
        print(
            f"static art verified: {WIDTH}x{HEIGHT}, {black_pixels} black pixels, "
            f"packed sha256 {packed_sha}"
        )
        return 0

    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    print(
        f"generated static art: {WIDTH}x{HEIGHT}, {black_pixels} black pixels, "
        f"packed sha256 {packed_sha}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
