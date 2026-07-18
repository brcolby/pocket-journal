from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import string
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / "tools/generate_icon_assets.py"
SPEC = importlib.util.spec_from_file_location("generate_icon_assets", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


class CarbonAssetContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = assets.load_and_validate_manifest()
        cls.icons = assets.generate_icon_records(cls.manifest)
        cls.base_glyphs = assets.generate_carbon_glyph_records(cls.manifest)
        cls.glyphs = cls.base_glyphs + assets.generate_composites(cls.base_glyphs)

    def test_source_manifest_is_complete_and_hashed(self) -> None:
        sources = self.manifest["sources"]
        active = [source for source in sources if source["status"] == "active"]
        reference = [source for source in sources if source["status"] == "reference"]
        self.assertEqual((len(active), len(reference), len(sources)), (73, 26, 99))
        self.assertEqual(len(list((ROOT / "assets/icons/carbon/svg/32").glob("*.svg"))), 99)
        self.assertTrue(all(re.fullmatch(r"[0-9a-f]{64}", source["sha256"]) for source in sources))
        self.assertTrue(all(source["typed_ids"] and source["allowed_sizes"] for source in active))
        self.assertTrue(all(not source["typed_ids"] and not source["allowed_sizes"] for source in reference))
        self.assertEqual(self.manifest["package"]["version"], "11.82.0")
        self.assertEqual(
            self.manifest["package"]["tarball_sha1"],
            "c5ae9cc66e2698db1f05a8110e051c7b94eed8df",
        )

    def test_legacy_arrows_are_strictly_reference_only(self) -> None:
        arrows = {f"svg/32/arrow--{direction}.svg" for direction in ("up", "down", "left", "right")}
        entries = {source["path"]: source for source in self.manifest["sources"] if source["path"] in arrows}
        self.assertEqual(set(entries), arrows)
        self.assertTrue(all(source["status"] == "reference" for source in entries.values()))
        self.assertFalse(any("ARROW" in record["id"] for record in self.icons))

    def test_letter_groups_use_painted_element_clustering(self) -> None:
        expected = {
            "letter--Ii.svg": ([0], [1, 2]),
            "letter--Ll.svg": ([0], [1]),
            "letter--Tt.svg": ([0], [1]),
            "letter--Rr.svg": ([0], [1]),
            "letter--Ff.svg": ([0], [1]),
            "letter--Mm.svg": ([1], [0]),
            "letter--Ww.svg": ([1], [0]),
            "letter--Jj.svg": ([2], [0, 1]),
        }
        for filename, groups in expected.items():
            with self.subTest(filename=filename):
                source = next(source for source in self.manifest["sources"] if source["path"].endswith(filename))
                extraction = source["glyph_extraction"]
                self.assertEqual(extraction["method"], "painted_element_horizontal_clustering")
                self.assertFalse(extraction["split_at_x_16"])
                self.assertEqual(tuple(group["elements"] for group in extraction["groups"]), groups)

    def test_case_is_preserved_and_descenders_survive(self) -> None:
        by_key = {(record["id"], record["size"]): record["bitmap"] for record in self.base_glyphs}
        for letter in "ILT RFMWJ".replace(" ", ""):
            with self.subTest(letter=letter):
                upper = by_key[(f"PJ_CARBON_GLYPH_UPPER_{letter}", 32)]
                lower = by_key[(f"PJ_CARBON_GLYPH_LOWER_{letter}", 32)]
                self.assertNotEqual(upper.rows, lower.rows)
        for letter in "gjy":
            with self.subTest(descender=letter):
                lower = by_key[(f"PJ_CARBON_GLYPH_LOWER_{letter.upper()}", 32)]
                self.assertIsNotNone(lower.ink_bbox)
                assert lower.ink_bbox is not None
                self.assertGreaterEqual(lower.ink_bbox[3], 27)

    def test_72_glyph_identities_have_all_exact_sizes(self) -> None:
        identities = {record["id"] for record in self.base_glyphs}
        self.assertEqual(len(identities), 72)
        for typed_id in identities:
            with self.subTest(typed_id=typed_id):
                records = [record for record in self.base_glyphs if record["id"] == typed_id]
                self.assertEqual({record["size"] for record in records}, {16, 24, 32, 64})
                self.assertTrue(all(record["bitmap"].advance == record["size"] // 2 for record in records))

    def test_regular_digits_are_fixed_width_and_distinct(self) -> None:
        regular = [record for record in self.base_glyphs if record["kind"] == "number"]
        for size in assets.TEXT_SIZES:
            records = [record for record in regular if record["size"] == size]
            self.assertEqual(len(records), 10)
            self.assertEqual({record["bitmap"].advance for record in records}, {size // 2})
            one = next(record["bitmap"].rows for record in records if record["id"].endswith("_1"))
            nine = next(record["bitmap"].rows for record in records if record["id"].endswith("_9"))
            self.assertNotEqual(one, nine)

    def test_settings_composites_use_only_small_digits_and_lowercase_h(self) -> None:
        composites = [record for record in self.glyphs if record["kind"] == "settings_composite"]
        base_by_key = {
            (record["id"], record["size"]): record["bitmap"]
            for record in self.base_glyphs
        }
        self.assertEqual([record["id"] for record in composites], [
            "PJ_CARBON_GLYPH_SETTINGS_12H",
            "PJ_CARBON_GLYPH_SETTINGS_24H",
        ])
        self.assertNotEqual(composites[0]["bitmap"].rows, composites[1]["bitmap"].rows)
        for record in composites:
            self.assertEqual((record["bitmap"].width, record["bitmap"].height), (64, 64))
            self.assertIn("PJ_CARBON_GLYPH_LOWER_H@32", record["source"])
            self.assertNotIn("PJ_CARBON_GLYPH_UPPER_H", record["source"])
            self.assertNotIn("PJ_CARBON_GLYPH_DIGIT_", record["source"])
        small_digit = base_by_key[("PJ_CARBON_GLYPH_SMALL_DIGIT_2", 64)]
        lowercase_h = base_by_key[("PJ_CARBON_GLYPH_LOWER_H", 32)]
        assert small_digit.ink_bbox is not None and lowercase_h.ink_bbox is not None
        self.assertLess(
            lowercase_h.ink_bbox[3] - lowercase_h.ink_bbox[1],
            small_digit.ink_bbox[3] - small_digit.ink_bbox[1],
        )

    def test_exact_semantic_record_contract(self) -> None:
        expected = {
            (source["typed_ids"][0], size)
            for source in self.manifest["sources"]
            if source["kind"] == "semantic_icon"
            for size in source["allowed_sizes"]
        }
        actual = {(record["id"], record["size"]) for record in self.icons}
        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 30)
        self.assertIn(("PJ_CARBON_ICON_VOLUME_UP", 40), actual)
        self.assertIn(("PJ_CARBON_ICON_VOLUME_UP", 64), actual)
        self.assertIn(("PJ_CARBON_ICON_TOGGLE_OFF", 56), actual)
        self.assertIn(("PJ_CARBON_ICON_TOGGLE_ON", 56), actual)
        self.assertNotIn(("PJ_CARBON_ICON_TOGGLE_OFF", 40), actual)
        self.assertNotIn(("PJ_CARBON_ICON_TOGGLE_ON", 40), actual)
        self.assertNotIn(("PJ_CARBON_ICON_STOP", 40), actual)

    def test_punctuation_asset_contains_no_letters_or_numbers(self) -> None:
        document = json.loads(
            (ROOT / "simulator/assets/fonts/ibm-plex-mono-bold-punctuation-1bit.json").read_text()
        )
        codepoints = {record["codepoint"] for record in document["records"]}
        self.assertEqual(codepoints, {ord(char) for char in " " + string.punctuation})
        self.assertFalse(any(chr(codepoint).isalnum() for codepoint in codepoints))
        self.assertEqual(document["fallback_codepoint"], ord("?"))
        self.assertEqual(document["family"], "IBM Plex Mono Bold")
        self.assertFalse((ROOT / "firmware/components/pj_ui/include/pj_font_space_mono.h").exists())
        self.assertFalse((ROOT / "simulator/assets/fonts/space-mono-bold-1bit.json").exists())

    def test_generator_and_firmware_api_have_no_legacy_resolution_paths(self) -> None:
        generator = GENERATOR_PATH.read_text()
        firmware = "\n".join(
            path.read_text()
            for path in (
                ROOT / "firmware/components/pj_ui/include/pj_icon_carbon.h",
                ROOT / "firmware/components/pj_ui/include/pj_glyph_carbon.h",
                ROOT / "firmware/components/pj_ui/include/pj_font_ibm_plex_mono_bold.h",
            )
        )
        self.assertNotIn("qlmanage", generator)
        self.assertNotIn("thumbnail(", generator)
        self.assertNotIn("strcmp", firmware)
        self.assertNotIn("const char *name", firmware)
        self.assertNotIn("PJ_FONT_SPACE_MONO", firmware)

    def test_firmware_and_simulator_records_have_matching_bits(self) -> None:
        document = json.loads((ROOT / "simulator/assets/icons/carbon-1bit.json").read_text())
        simulator = {(record["id"], record["size"]): record for record in document["records"]}
        for record in self.icons:
            with self.subTest(record=(record["id"], record["size"])):
                sim_record = simulator[(record["id"], record["size"])]
                self.assertEqual(tuple(sim_record["rows"]), record["bitmap"].rows)
                self.assertEqual(sim_record["stride"], record["bitmap"].stride)

    def test_typed_headers_compile_and_reject_unlisted_sizes(self) -> None:
        source = r'''
#include <assert.h>
#include "pj_icon_carbon.h"
#include "pj_glyph_carbon.h"
#include "pj_font_ibm_plex_mono_bold.h"

int main(void) {
    assert(PJ_CARBON_ICON_BITMAP_COUNT == 30);
    assert(PJ_CARBON_DERIVED_GLYPH_COUNT == 72);
    assert(pj_carbon_icon_lookup(PJ_CARBON_ICON_TIME, 64) != 0);
    assert(pj_carbon_icon_lookup(PJ_CARBON_ICON_TIME, 40) == 0);
    assert(pj_carbon_icon_lookup(PJ_CARBON_ICON_PLAY_FILLED, 96) != 0);
    assert(pj_carbon_icon_lookup(PJ_CARBON_ICON_PLAY_FILLED, 144) == 0);
    assert(pj_carbon_glyph_lookup_codepoint('A', 32) != 0);
    assert(pj_carbon_glyph_lookup_codepoint('a', 32) != 0);
    assert(pj_carbon_glyph_lookup_codepoint('A', 31) == 0);
    assert(pj_carbon_glyph_lookup_codepoint('A', 32) != pj_carbon_glyph_lookup_codepoint('a', 32));
    assert(pj_ibm_plex_punctuation_lookup(':', 32) != 0);
    assert(pj_ibm_plex_punctuation_lookup(0x2603, 32) == pj_ibm_plex_punctuation_lookup('?', 32));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="pj-carbon-header-test-") as temporary:
            source_path = Path(temporary) / "contract.c"
            executable = Path(temporary) / "contract"
            source_path.write_text(source)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'firmware/components/pj_ui/include'}",
                    str(source_path),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_generation_is_byte_identical_and_checked_in(self) -> None:
        first = assets.build_outputs(self.manifest)
        second = assets.build_outputs(self.manifest)
        self.assertEqual(first, second)
        self.assertEqual(assets.check_outputs(first), [])
        gallery = first[assets.GALLERY_PATH].decode()
        self.assertIn("Compiled semantic records (30)", gallery)
        self.assertIn("Derived letters (52)", gallery)
        self.assertIn("Digit variants (20)", gallery)
        self.assertIn("Active upstream sources (73)", gallery)
        self.assertIn("Reference-only upstream sources (26)", gallery)
        self.assertIn("canvas class=\"pixel light\"", gallery)
        self.assertIn("canvas class=\"pixel dark\"", gallery)


if __name__ == "__main__":
    unittest.main()
