from __future__ import annotations

import asyncio
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from pocket_journal_partner.ble import (
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

    def __init__(self, device) -> None:
        self.device = device

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        _ = (exc_type, exc, tb)

    async def read_gatt_char(self, uuid: str) -> bytes:
        _ = uuid
        return b'{"device_id":"pj-test","state":"stored","wifi":"unavailable"}'

    async def write_gatt_char(self, uuid: str, data: bytes, response: bool) -> None:
        self.writes.append((uuid, data, response))


class BleProvisioningTests(unittest.TestCase):
    def test_provisions_separate_bounded_characteristics(self) -> None:
        fake_bleak = SimpleNamespace(BleakScanner=FakeScanner, BleakClient=FakeClient)
        FakeClient.writes = []
        with patch.dict(sys.modules, {"bleak": fake_bleak}):
            result = asyncio.run(provision_wifi("PJ-TEST", "Lab WiFi", "secret"))

        self.assertEqual(result.device_id, "pj-test")
        self.assertEqual(result.ble_name, "PJ-TEST")
        self.assertEqual(FakeClient.writes[0], (SSID_UUID, b"Lab WiFi", True))
        self.assertEqual(FakeClient.writes[1], (PASSWORD_UUID, b"secret", True))
        self.assertEqual(FakeClient.writes[2][0], TOKEN_UUID)
        self.assertTrue(FakeClient.writes[2][1])
        self.assertEqual(FakeClient.writes[3], (COMMIT_UUID, b"\x01", True))


if __name__ == "__main__":
    unittest.main()
