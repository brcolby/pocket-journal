import json
import math
import subprocess
import sys
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools" / "generate_dxf_geometry.py"
JSON_PATH = ROOT / "simulator" / "assets" / "dxf-layout-geometry.json"
GENERATED_PATHS = (
    Path("firmware/components/pj_ui/include/pj_layout_geometry.h"),
    Path("firmware/components/pj_ui/pj_layout_geometry.c"),
    Path("simulator/assets/dxf-layout-geometry.json"),
)


def point_on_segment(point, first, second):
    cross = (point[0] - first[0]) * (second[1] - first[1]) - (
        point[1] - first[1]
    ) * (second[0] - first[0])
    return (
        cross == 0
        and min(first[0], second[0]) <= point[0] <= max(first[0], second[0])
        and min(first[1], second[1]) <= point[1] <= max(first[1], second[1])
    )


def point_in_polygon(point, polygon):
    inside = False
    for index, first in enumerate(polygon):
        second = polygon[(index + 1) % len(polygon)]
        if point_on_segment(point, first, second):
            return True
        if (first[1] > point[1]) == (second[1] > point[1]):
            continue
        left = (point[0] - first[0]) * (second[1] - first[1])
        right = (point[1] - first[1]) * (second[0] - first[0])
        if (second[1] > first[1] and left < right) or (
            second[1] < first[1] and left > right
        ):
            inside = not inside
    return inside


def unpack_point(point):
    return point["x"], point["y"]


def canonical_segment(rule):
    return tuple(sorted((unpack_point(rule["start"]), unpack_point(rule["end"]))))


class DxfGeometryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payload = json.loads(JSON_PATH.read_text(encoding="utf-8"))
        cls.layouts = {layout["id"]: layout for layout in cls.payload["layouts"]}
        cls.scale = cls.payload["display"]["coordinate_scale"]
        cls.maximum = cls.payload["display"]["width"] * cls.scale

    def polygon(self, slot):
        return [unpack_point(point) for point in slot["hit_polygon"]]

    def hit_matches(self, layout, point):
        return [
            slot["id"]
            for slot in layout["slots"]
            if point_in_polygon(point, self.polygon(slot))
        ]

    def test_generator_check_and_two_clean_runs_are_byte_identical(self):
        subprocess.run([sys.executable, str(GENERATOR), "--check"], cwd=ROOT, check=True)
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            subprocess.run(
                [sys.executable, str(GENERATOR), "--output-root", first],
                cwd=ROOT,
                check=True,
                stdout=subprocess.DEVNULL,
            )
            subprocess.run(
                [sys.executable, str(GENERATOR), "--output-root", second],
                cwd=ROOT,
                check=True,
                stdout=subprocess.DEVNULL,
            )
            for relative_path in GENERATED_PATHS:
                self.assertEqual(
                    (Path(first) / relative_path).read_bytes(),
                    (Path(second) / relative_path).read_bytes(),
                    relative_path,
                )

    def test_sources_have_only_expected_outer_and_interior_lines(self):
        expected = {
            "home_3_1": (7, 3),
            "notes_3_1m": (7, 3),
            "time_4_1": (9, 5),
            "settings_4_0m": (9, 5),
        }
        for layout_id, (source_count, rule_count) in expected.items():
            layout = self.layouts[layout_id]
            self.assertEqual(layout["source_line_count"], source_count)
            self.assertEqual(layout["discarded_outer_line_count"], 4)
            self.assertEqual(len(layout["rules"]), rule_count)
            self.assertEqual(len(layout["slots"]), rule_count if rule_count == 3 else 4)
            for rule in layout["rules"]:
                first, second = canonical_segment(rule)
                is_complete_horizontal = (
                    first[1] == second[1]
                    and first[1] in (0, self.maximum)
                    and first[0] == 0
                    and second[0] == self.maximum
                )
                is_complete_vertical = (
                    first[0] == second[0]
                    and first[0] in (0, self.maximum)
                    and first[1] == 0
                    and second[1] == self.maximum
                )
                self.assertFalse(is_complete_horizontal or is_complete_vertical)

    def test_dxf_y_is_inverted_and_settings_is_the_exact_geometry_mirror(self):
        home = self.layouts["home_3_1"]
        boundary_endpoints = [
            point
            for rule in home["rules"]
            for point in (unpack_point(rule["start"]), unpack_point(rule["end"]))
            if point[0] in (0, self.maximum) or point[1] in (0, self.maximum)
        ]
        self.assertIn((1135, 0), boundary_endpoints)
        self.assertIn((0, 3040), boundary_endpoints)
        self.assertIn((self.maximum, 2503), boundary_endpoints)

        time_rules = {canonical_segment(rule) for rule in self.layouts["time_4_1"]["rules"]}
        mirrored_time_rules = {
            tuple(
                sorted(
                    (
                        (self.maximum - first[0], first[1]),
                        (self.maximum - second[0], second[1]),
                    )
                )
            )
            for first, second in time_rules
        }
        settings_rules = {
            canonical_segment(rule) for rule in self.layouts["settings_4_0m"]["rules"]
        }
        self.assertEqual(settings_rules, mirrored_time_rules)
        self.assertEqual(self.layouts["settings_4_0m"]["horizontal_mirror_of"], "time_4_1")

    def test_all_40000_pixel_centers_have_stable_slot_ownership(self):
        for layout in self.payload["layouts"]:
            owned = {slot["id"]: 0 for slot in layout["slots"]}
            for y in range(200):
                for x in range(200):
                    point = (x * self.scale + self.scale // 2, y * self.scale + self.scale // 2)
                    matches = self.hit_matches(layout, point)
                    self.assertTrue(matches, (layout["id"], x, y))
                    # The public hit-test contract takes the first matching slot.
                    owned[matches[0]] += 1
            self.assertEqual(sum(owned.values()), 40000)
            self.assertTrue(all(count > 0 for count in owned.values()))

            # Every finite DXF separator is shared by two faces away from a junction,
            # and stable source slot order determines which face owns the line.
            for rule in layout["rules"]:
                first = unpack_point(rule["start"])
                second = unpack_point(rule["end"])
                midpoint = (
                    Fraction(first[0] + second[0], 2),
                    Fraction(first[1] + second[1], 2),
                )
                matches = self.hit_matches(layout, midpoint)
                self.assertEqual(len(matches), 2, (layout["id"], rule, matches))
                expected = next(
                    slot["id"]
                    for slot in layout["slots"]
                    if slot["id"] in matches
                )
                self.assertEqual(matches[0], expected)

    def test_representative_sector_centers_and_icon_centroids(self):
        representative_pixels = {
            "home_3_1": ((20, 80), (100, 180), (180, 80)),
            "notes_3_1m": ((100, 20), (20, 120), (180, 120)),
            "time_4_1": ((40, 40), (160, 40), (40, 160), (160, 160)),
            "settings_4_0m": ((40, 40), (160, 40), (40, 160), (160, 160)),
        }
        for layout_id, pixels in representative_pixels.items():
            layout = self.layouts[layout_id]
            self.assertEqual(len(pixels), len(layout["slots"]))
            for slot, (x, y) in zip(layout["slots"], pixels):
                point = (x * self.scale + self.scale // 2, y * self.scale + self.scale // 2)
                self.assertEqual(self.hit_matches(layout, point)[0], slot["id"])
                center = unpack_point(slot["icon_center"])
                self.assertEqual(self.hit_matches(layout, center)[0], slot["id"])

    def test_four_sector_center_diagonal_is_preserved(self):
        for layout_id in ("time_4_1", "settings_4_0m"):
            layout = self.layouts[layout_id]
            interior_rules = []
            for rule in layout["rules"]:
                first, second = canonical_segment(rule)
                if all(0 < coordinate < self.maximum for point in (first, second) for coordinate in point):
                    interior_rules.append((first, second))
            self.assertEqual(len(interior_rules), 1)
            first, second = interior_rules[0]
            length_px = math.hypot(second[0] - first[0], second[1] - first[1]) / self.scale
            self.assertGreater(length_px, 14.0)
            self.assertLess(length_px, 16.0)

    def test_rules_use_the_exact_four_pixel_raster_token_without_an_outer_box(self):
        self.assertEqual(self.payload["rule_width_px"], 4)
        radius = self.payload["rule_width_px"] * self.scale // 2

        def within_rule(point, rule):
            first = unpack_point(rule["start"])
            second = unpack_point(rule["end"])
            vx, vy = second[0] - first[0], second[1] - first[1]
            wx, wy = point[0] - first[0], point[1] - first[1]
            length_squared = vx * vx + vy * vy
            projection = wx * vx + wy * vy
            if projection <= 0:
                return wx * wx + wy * wy <= radius * radius
            if projection >= length_squared:
                dx, dy = point[0] - second[0], point[1] - second[1]
                return dx * dx + dy * dy <= radius * radius
            cross = wx * vy - wy * vx
            return cross * cross <= radius * radius * length_squared

        scans = (("home_3_1", 10), ("notes_3_1m", 190), ("time_4_1", 190), ("settings_4_0m", 190))
        for layout_id, y in scans:
            layout = self.layouts[layout_id]
            covered = [
                x
                for x in range(200)
                if any(
                    within_rule(
                        (x * self.scale + self.scale // 2, y * self.scale + self.scale // 2),
                        rule,
                    )
                    for rule in layout["rules"]
                )
            ]
            self.assertEqual(len(covered), 4, (layout_id, y, covered))

            for corner in ((0, 0), (199, 0), (0, 199), (199, 199)):
                point = (
                    corner[0] * self.scale + self.scale // 2,
                    corner[1] * self.scale + self.scale // 2,
                )
                self.assertFalse(any(within_rule(point, rule) for rule in layout["rules"]))


if __name__ == "__main__":
    unittest.main()
