#!/usr/bin/env python3
"""Unit tests for the signed firmware app-size gate."""

from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "firmware" / "tools" / "check_app_size.py"
SPEC = importlib.util.spec_from_file_location("check_app_size", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
check_app_size = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = check_app_size
SPEC.loader.exec_module(check_app_size)


def entry(
    label: str, subtype: int, offset: int = 0x10000, size: int = 0x200000
) -> bytes:
    raw_label = label.encode("ascii").ljust(16, b"\0")
    return struct.pack(
        "<HBBII16sI", 0x50AA, 0x00, subtype, offset, size, raw_label, 0
    )


def table(*entries: bytes) -> bytes:
    data = b"".join(entries) + b"\xff" * 32
    return data.ljust(0xC00, b"\xff")


def valid_table() -> bytes:
    return table(
        entry("factory", 0x00),
        entry("ota_0", 0x10, 0x210000),
        entry("ota_1", 0x11, 0x410000),
    )


class AppSizeBudgetTests(unittest.TestCase):
    def test_secure_boot_v2_projection_includes_padding_and_signature(self) -> None:
        self.assertEqual(
            check_app_size.secure_boot_v2_signed_size(0x190000), 0x191000
        )
        self.assertEqual(
            check_app_size.secure_boot_v2_signed_size(0x190001), 0x1A1000
        )

    def test_exact_budget_accepts_all_three_slots(self) -> None:
        partitions = check_app_size.parse_partition_table(valid_table())
        checked = check_app_size.check_budget(
            0x1A0000, partitions, 0x60000, configured_max=0x1A0000
        )
        self.assertEqual(
            [(partition.label, headroom) for partition, headroom in checked],
            [("factory", 0x60000), ("ota_0", 0x60000), ("ota_1", 0x60000)],
        )

    def test_image_that_fits_partition_but_consumes_reserve_is_rejected(self) -> None:
        partitions = check_app_size.parse_partition_table(valid_table())
        with self.assertRaisesRegex(check_app_size.BudgetError, "factory leaves"):
            check_app_size.check_budget(0x1A0001, partitions, 0x60000)

    def test_missing_ota_slot_is_rejected(self) -> None:
        partitions = check_app_size.parse_partition_table(
            table(entry("factory", 0x00), entry("ota_0", 0x10))
        )
        with self.assertRaisesRegex(check_app_size.BudgetError, "missing: ota_1"):
            check_app_size.check_budget(0x100000, partitions, 0x60000)

    def test_mislabeled_subtype_is_rejected(self) -> None:
        partitions = check_app_size.parse_partition_table(
            table(
                entry("factory", 0x00),
                entry("ota_0", 0x11),
                entry("ota_1", 0x10),
            )
        )
        with self.assertRaisesRegex(check_app_size.BudgetError, "ota_0 has subtype"):
            check_app_size.check_budget(0x100000, partitions, 0x60000)

    def test_runtime_ota_limit_cannot_exceed_build_budget(self) -> None:
        partitions = check_app_size.parse_partition_table(valid_table())
        with self.assertRaisesRegex(check_app_size.BudgetError, "configured OTA maximum"):
            check_app_size.check_budget(
                0x100000, partitions, 0x60000, configured_max=0x200000
            )

    def test_corrupt_partition_table_is_rejected(self) -> None:
        with self.assertRaisesRegex(check_app_size.BudgetError, "invalid partition magic"):
            check_app_size.parse_partition_table(b"\0" * 32)

    def test_cli_checks_generated_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "app.bin"
            partition_table = root / "partition-table.bin"
            image.write_bytes(b"\0" * 0x10000)
            partition_table.write_bytes(valid_table())
            with contextlib.redirect_stdout(io.StringIO()):
                result = check_app_size.main(
                    [
                        "--image",
                        str(image),
                        "--partition-table",
                        str(partition_table),
                        "--min-headroom",
                        "0x60000",
                        "--configured-max",
                        "0x1a0000",
                    ]
                )
            self.assertEqual(result, 0)

    def test_cli_rejects_unsigned_image_whose_signed_form_exceeds_budget(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "app.bin"
            partition_table = root / "partition-table.bin"
            with image.open("wb") as output:
                output.truncate(0x190001)
            partition_table.write_bytes(valid_table())
            with contextlib.redirect_stderr(io.StringIO()):
                result = check_app_size.main(
                    [
                        "--image",
                        str(image),
                        "--partition-table",
                        str(partition_table),
                        "--min-headroom",
                        "0x60000",
                        "--configured-max",
                        "0x1a0000",
                        "--unsigned-secure-boot-v2",
                    ]
                )
            self.assertEqual(result, 1)


if __name__ == "__main__":
    unittest.main()
