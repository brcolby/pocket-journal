from __future__ import annotations

import asyncio
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from pocket_journal_partner.ble import (
    BleProvisioningError,
    COMMIT_UUID,
    PASSWORD_UUID,
    SSID_UUID,
    TOKEN_UUID,
    provision_wifi,
)


class FakeScanner:
    device = SimpleNamespace(name="PJ-TEST")

    @classmethod
    async def find_device_by_name(cls, name: str, timeout: float):
        _ = timeout
        return cls.device if name == cls.device.name else None

    @classmethod
    async def discover(cls, timeout: float):
        _ = timeout
        return [cls.device]


class FakeClient:
    writes: list[tuple[str, bytes, bool]] = []
    pair_result = True
    pair_error: Exception | None = None
    fail_writes = False

    def __init__(self, device) -> None:
        self.device = device

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        _ = (exc_type, exc, tb)

    async def read_gatt_char(self, uuid: str) -> bytes:
        _ = uuid
        return b'{"device_id":"pj-test","state":"stored","wifi":"unavailable"}'

    async def pair(self) -> bool:
        if self.pair_error is not None:
            raise self.pair_error
        return self.pair_result

    async def write_gatt_char(self, uuid: str, data: bytes, response: bool) -> None:
        if self.fail_writes:
            raise RuntimeError("insufficient authentication")
        self.writes.append((uuid, data, response))


class BleProvisioningTests(unittest.TestCase):
    def test_provisions_separate_bounded_characteristics(self) -> None:
        fake_bleak = SimpleNamespace(BleakScanner=FakeScanner, BleakClient=FakeClient)
        FakeClient.writes = []
        FakeClient.pair_result = True
        FakeClient.pair_error = None
        FakeClient.fail_writes = False
        with patch.dict(sys.modules, {"bleak": fake_bleak}):
            result = asyncio.run(provision_wifi("PJ-TEST", "Lab WiFi", "secret"))

        self.assertEqual(result.device_id, "pj-test")
        self.assertEqual(result.ble_name, "PJ-TEST")
        self.assertEqual(FakeClient.writes[0], (SSID_UUID, b"Lab WiFi", True))
        self.assertEqual(FakeClient.writes[1], (PASSWORD_UUID, b"secret", True))
        self.assertEqual(FakeClient.writes[2][0], TOKEN_UUID)
        self.assertTrue(FakeClient.writes[2][1])
        self.assertEqual(FakeClient.writes[3], (COMMIT_UUID, b"\x01", True))

    def test_authentication_failure_is_actionable(self) -> None:
        fake_bleak = SimpleNamespace(BleakScanner=FakeScanner, BleakClient=FakeClient)
        FakeClient.writes = []
        FakeClient.pair_result = True
        FakeClient.pair_error = None
        FakeClient.fail_writes = True
        with patch.dict(sys.modules, {"bleak": fake_bleak}):
            with self.assertRaisesRegex(BleProvisioningError, "encrypted paired connection"):
                asyncio.run(provision_wifi("PJ-TEST", "Lab WiFi", "secret"))
        self.assertEqual(FakeClient.writes, [])

    def test_firmware_marks_provisioning_writes_encrypted(self) -> None:
        board_source = Path(__file__).parents[2] / "firmware/components/pj_board/pj_board.c"
        source = board_source.read_text(encoding="utf-8")
        self.assertGreaterEqual(source.count("BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC"), 4)
        self.assertIn("BLE_ATT_ERR_INSUFFICIENT_AUTHEN", source)
        self.assertIn("ble_connection_encrypted(conn_handle)", source)
        self.assertIn("ble_hs_cfg.sm_sc = 1", source)
        self.assertIn("ble_hs_cfg.sm_bonding = 1", source)

    def test_pairing_rejection_does_not_send_credentials(self) -> None:
        fake_bleak = SimpleNamespace(BleakScanner=FakeScanner, BleakClient=FakeClient)
        FakeClient.writes = []
        FakeClient.pair_result = False
        FakeClient.pair_error = None
        FakeClient.fail_writes = False
        with patch.dict(sys.modules, {"bleak": fake_bleak}):
            with self.assertRaisesRegex(BleProvisioningError, "rejected"):
                asyncio.run(provision_wifi("PJ-TEST", "Lab WiFi", "secret"))
        self.assertEqual(FakeClient.writes, [])


if __name__ == "__main__":
    unittest.main()
