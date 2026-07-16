#!/usr/bin/env python3
"""Enforce signed application headroom across every Pocket Journal app slot."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import sys


ENTRY = struct.Struct("<HBBII16sI")
PARTITION_MAGIC = 0x50AA
PARTITION_TYPE_APP = 0x00
REQUIRED_APP_SLOTS = {
    "factory": 0x00,
    "ota_0": 0x10,
    "ota_1": 0x11,
}
SECURE_BOOT_V2_ALIGNMENT = 0x10000
SECURE_BOOT_V2_SIGNATURE_SECTOR = 0x1000


class BudgetError(ValueError):
    """The generated image or partition table violates the app budget."""


@dataclass(frozen=True)
class Partition:
    label: str
    partition_type: int
    subtype: int
    offset: int
    size: int


def positive_int(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid integer: {value}") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def parse_partition_table(data: bytes) -> list[Partition]:
    if len(data) % ENTRY.size != 0:
        raise BudgetError(
            f"partition table length {len(data)} is not a multiple of {ENTRY.size}"
        )

    partitions: list[Partition] = []
    for offset in range(0, len(data), ENTRY.size):
        entry = data[offset : offset + ENTRY.size]
        if entry == b"\xff" * ENTRY.size:
            break
        magic, partition_type, subtype, address, size, raw_label, _flags = (
            ENTRY.unpack(entry)
        )
        if magic != PARTITION_MAGIC:
            # ESP-IDF may append an MD5 record after the last real partition.
            if magic == 0xEBEB:
                break
            raise BudgetError(
                f"invalid partition magic 0x{magic:04x} at table offset 0x{offset:x}"
            )
        label = raw_label.split(b"\0", 1)[0].decode("ascii", errors="strict")
        partitions.append(
            Partition(label, partition_type, subtype, address, size)
        )
    return partitions


def check_budget(
    image_size: int,
    partitions: list[Partition],
    min_headroom: int,
    configured_max: int | None = None,
) -> list[tuple[Partition, int]]:
    by_label: dict[str, Partition] = {}
    for partition in partitions:
        if partition.label in by_label:
            raise BudgetError(f"duplicate partition label: {partition.label}")
        by_label[partition.label] = partition

    checked: list[tuple[Partition, int]] = []
    for label, expected_subtype in REQUIRED_APP_SLOTS.items():
        partition = by_label.get(label)
        if partition is None:
            raise BudgetError(f"required app partition is missing: {label}")
        if partition.partition_type != PARTITION_TYPE_APP:
            raise BudgetError(f"{label} is not an app partition")
        if partition.subtype != expected_subtype:
            raise BudgetError(
                f"{label} has subtype 0x{partition.subtype:02x}, "
                f"expected 0x{expected_subtype:02x}"
            )
        headroom = partition.size - image_size
        if headroom < min_headroom:
            raise BudgetError(
                f"{label} leaves 0x{max(headroom, 0):x} bytes, "
                f"below the required 0x{min_headroom:x} headroom"
            )
        checked.append((partition, headroom))

    slot_budget = min(partition.size for partition, _headroom in checked) - min_headroom
    if configured_max is not None and configured_max > slot_budget:
        raise BudgetError(
            f"configured OTA maximum 0x{configured_max:x} exceeds the "
            f"headroom budget 0x{slot_budget:x}"
        )
    if configured_max is not None and image_size > configured_max:
        raise BudgetError(
            f"signed image 0x{image_size:x} exceeds configured OTA maximum "
            f"0x{configured_max:x}"
        )
    return checked


def secure_boot_v2_signed_size(unsigned_size: int) -> int:
    """Return ESP-IDF's padded image plus one Secure Boot V2 signature sector."""
    aligned_size = (
        (unsigned_size + SECURE_BOOT_V2_ALIGNMENT - 1)
        // SECURE_BOOT_V2_ALIGNMENT
        * SECURE_BOOT_V2_ALIGNMENT
    )
    return aligned_size + SECURE_BOOT_V2_SIGNATURE_SECTOR


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--partition-table", required=True, type=Path)
    parser.add_argument("--min-headroom", required=True, type=positive_int)
    parser.add_argument("--configured-max", type=positive_int)
    parser.add_argument(
        "--unsigned-secure-boot-v2",
        action="store_true",
        help="project 64 KiB padding and the 4 KiB signature sector",
    )
    args = parser.parse_args(argv)

    try:
        image_size = args.image.stat().st_size
        budgeted_size = (
            secure_boot_v2_signed_size(image_size)
            if args.unsigned_secure_boot_v2
            else image_size
        )
        partitions = parse_partition_table(args.partition_table.read_bytes())
        checked = check_budget(
            budgeted_size, partitions, args.min_headroom, args.configured_max
        )
    except (BudgetError, OSError, UnicodeError) as error:
        print(f"app-size budget failed: {error}", file=sys.stderr)
        return 1

    size_text = f"signed=0x{image_size:x}"
    if args.unsigned_secure_boot_v2:
        size_text = (
            f"unsigned=0x{image_size:x}, projected-signed=0x{budgeted_size:x}"
        )
    print(
        f"app-size budget passed: {size_text}, "
        f"minimum-headroom=0x{args.min_headroom:x}"
    )
    for partition, headroom in checked:
        print(
            f"  {partition.label}: offset=0x{partition.offset:x} "
            f"size=0x{partition.size:x} headroom=0x{headroom:x}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
