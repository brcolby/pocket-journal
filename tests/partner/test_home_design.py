from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import json
import unittest

from pocket_journal_partner.cli import _load_home_design, _load_static_art
from pocket_journal_partner.home_design import normalize_home_design


class HomeDesignTests(unittest.TestCase):
    def test_loads_normalized_home_design(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "home.json"
            path.write_text(json.dumps({
                "title": "Pocket",
                "slots": [
                    {"label": "Notes", "icon": "notebook", "state": "notes"},
                    {"label": "Sync", "icon": "wifi", "state": "sync"},
                ],
            }), encoding="utf-8")
            design = _load_home_design(str(path))
            self.assertEqual(design["title"], "Pocket")
            self.assertEqual(design["slots"][1]["state"], "sync")

    def test_preserves_four_slot_order(self) -> None:
        design = normalize_home_design({
            "title": "Daily tools",
            "slots": [
                {"label": "Capture", "icon": "microphone", "state": "record"},
                {"label": "Read", "icon": "read_me", "state": "read"},
                {"label": "Timer", "icon": "timer", "state": "timer"},
                {"label": "Network", "icon": "wifi", "state": "sync"},
            ],
        })
        self.assertEqual([slot["state"] for slot in design["slots"]], ["record", "read", "timer", "sync"])

    def test_rejects_unknown_or_oversized_slots(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported home destination"):
            normalize_home_design({
                "slots": [{"label": "Missing", "icon": "notebook", "state": "removed"}],
            })
        with self.assertRaisesRegex(ValueError, "between one and four"):
            normalize_home_design({
                "slots": [
                    {"label": str(index), "icon": "notebook", "state": "notes"}
                    for index in range(5)
                ],
            })

    def test_loads_static_art_rows(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "static.json"
            path.write_text(json.dumps({
                "width": 200,
                "height": 200,
                "encoding": "rows",
                "rows": ["." * 200 for _ in range(200)],
            }), encoding="utf-8")
            art = _load_static_art(str(path))
            self.assertEqual(art["width"], 200)
            self.assertEqual(art["rows"][0], "0" * 200)

    def test_loads_static_art_pbm(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "static.pbm"
            pixels = ["0"] * (200 * 200)
            pixels[0] = "1"
            lines = ["P1", "200 200"]
            for row in range(200):
                lines.append(" ".join(pixels[row * 200:(row + 1) * 200]))
            path.write_text("\n".join(lines) + "\n", encoding="ascii")
            art = _load_static_art(str(path))
            self.assertEqual(art["rows"][0][0], "1")
            self.assertEqual(art["rows"][199], "0" * 200)


if __name__ == "__main__":
    unittest.main()
