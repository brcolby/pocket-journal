from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from pocket_journal_partner.config import DeviceProfile, PartnerConfig


class ConfigTests(unittest.TestCase):
    def test_round_trip(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            config = PartnerConfig(devices={
                "pj-test": DeviceProfile(
                    device_id="pj-test",
                    base_url="http://127.0.0.1",
                    token="token",
                    ble_name="PJ-TEST",
                )
            })
            config.save(path)
            loaded = PartnerConfig.load(path)
            self.assertEqual(loaded.devices["pj-test"].token, "token")


if __name__ == "__main__":
    unittest.main()

