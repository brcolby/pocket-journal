#!/usr/bin/env python3
"""Generate deterministic 200x200 UI geometry from the approved DXF sketches.

Only Python's standard library is used.  The generator parses LINE entities,
removes the four complete outer-box lines, inverts DXF Y, builds planar faces
from the remaining separators, and emits matching firmware and simulator data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from decimal import Decimal, ROUND_HALF_UP
from fractions import Fraction
from functools import cmp_to_key
from pathlib import Path
from typing import Iterable, Sequence


DISPLAY_WIDTH = 200
DISPLAY_HEIGHT = 200
COORD_SCALE = 16
DISPLAY_Q_X = DISPLAY_WIDTH * COORD_SCALE
DISPLAY_Q_Y = DISPLAY_HEIGHT * COORD_SCALE
RULE_WIDTH_PX = 4


@dataclass(frozen=True, order=True)
class Point:
    x: int
    y: int


@dataclass(frozen=True)
class SourceLine:
    x1: Decimal
    y1: Decimal
    x2: Decimal
    y2: Decimal


@dataclass(frozen=True)
class Segment:
    a: Point
    b: Point

    def canonical(self) -> tuple[Point, Point]:
        return tuple(sorted((self.a, self.b)))  # type: ignore[return-value]


@dataclass(frozen=True)
class SlotSpec:
    c_id: str
    json_id: str
    position: str
    anchor: Point


@dataclass(frozen=True)
class LayoutSpec:
    c_id: str
    json_id: str
    source_name: str
    slots: tuple[SlotSpec, ...]
    mirror_of: str | None = None


@dataclass(frozen=True)
class GeneratedSlot:
    spec: SlotSpec
    polygon: tuple[Point, ...]
    icon_center: Point


@dataclass(frozen=True)
class GeneratedLayout:
    spec: LayoutSpec
    source_sha256: str
    source_line_count: int
    outer_line_count: int
    rules: tuple[Segment, ...]
    slots: tuple[GeneratedSlot, ...]


def px(x: int, y: int) -> Point:
    return Point(x * COORD_SCALE, y * COORD_SCALE)


LAYOUT_SPECS = (
    LayoutSpec(
        "PJ_LAYOUT_HOME_3_1",
        "home_3_1",
        "3_1.dxf",
        (
            SlotSpec("PJ_LAYOUT_SLOT_HOME_TIME", "home_time", "left", px(35, 100)),
            SlotSpec("PJ_LAYOUT_SLOT_HOME_NOTES", "home_notes", "bottom", px(100, 165)),
            SlotSpec("PJ_LAYOUT_SLOT_HOME_SETTINGS", "home_settings", "right", px(165, 100)),
        ),
    ),
    LayoutSpec(
        "PJ_LAYOUT_NOTES_3_1M",
        "notes_3_1m",
        "3_1m.dxf",
        (
            SlotSpec("PJ_LAYOUT_SLOT_NOTES_RECORD", "notes_record", "top", px(100, 35)),
            SlotSpec("PJ_LAYOUT_SLOT_NOTES_LISTEN", "notes_listen", "left", px(35, 110)),
            SlotSpec("PJ_LAYOUT_SLOT_NOTES_READ", "notes_read", "right", px(165, 110)),
        ),
    ),
    LayoutSpec(
        "PJ_LAYOUT_TIME_4_1",
        "time_4_1",
        "4_1.dxf",
        (
            SlotSpec("PJ_LAYOUT_SLOT_TIME_ALARM", "time_alarm", "top_left", px(45, 45)),
            SlotSpec("PJ_LAYOUT_SLOT_TIME_STOPWATCH", "time_stopwatch", "top_right", px(155, 45)),
            SlotSpec("PJ_LAYOUT_SLOT_TIME_TIMER", "time_timer", "bottom_left", px(45, 155)),
            SlotSpec("PJ_LAYOUT_SLOT_TIME_INTERVAL", "time_interval", "bottom_right", px(155, 155)),
        ),
    ),
    LayoutSpec(
        "PJ_LAYOUT_SETTINGS_4_0M",
        "settings_4_0m",
        "4_0m.dxf",
        (
            SlotSpec("PJ_LAYOUT_SLOT_SETTINGS_VOLUME", "settings_volume", "top_left", px(45, 45)),
            SlotSpec("PJ_LAYOUT_SLOT_SETTINGS_THEME", "settings_theme", "top_right", px(155, 45)),
            SlotSpec("PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT", "settings_hour_format", "bottom_left", px(45, 155)),
            SlotSpec("PJ_LAYOUT_SLOT_SETTINGS_SYNC", "settings_sync", "bottom_right", px(155, 155)),
        ),
        mirror_of="time_4_1",
    ),
)


OUTPUT_PATHS = (
    Path("firmware/components/pj_ui/include/pj_layout_geometry.h"),
    Path("firmware/components/pj_ui/pj_layout_geometry.c"),
    Path("simulator/assets/dxf-layout-geometry.json"),
)


def parse_dxf_lines(path: Path) -> tuple[SourceLine, ...]:
    raw_lines = path.read_text(encoding="utf-8").splitlines()
    if len(raw_lines) % 2 != 0:
        raise ValueError(f"{path}: DXF group-code stream has an odd line count")

    pairs: list[tuple[int, str]] = []
    for index in range(0, len(raw_lines), 2):
        try:
            code = int(raw_lines[index].strip())
        except ValueError as error:
            raise ValueError(f"{path}:{index + 1}: invalid DXF group code") from error
        pairs.append((code, raw_lines[index + 1].strip()))

    in_entities = False
    awaiting_section_name = False
    current_type: str | None = None
    current_values: dict[int, str] = {}
    source_lines: list[SourceLine] = []

    def finish_entity() -> None:
        nonlocal current_type, current_values
        if current_type is None:
            return
        if current_type != "LINE":
            raise ValueError(f"{path}: unsupported {current_type} entity in ENTITIES section")
        required = (10, 20, 11, 21)
        missing = [code for code in required if code not in current_values]
        if missing:
            raise ValueError(f"{path}: LINE entity missing group codes {missing}")
        source_lines.append(
            SourceLine(*(Decimal(current_values[code]) for code in required))
        )
        current_type = None
        current_values = {}

    for code, value in pairs:
        if code == 0:
            if in_entities:
                finish_entity()
            if value == "SECTION":
                awaiting_section_name = True
            elif value == "ENDSEC":
                in_entities = False
            elif in_entities:
                current_type = value
                current_values = {}
            continue

        if awaiting_section_name and code == 2:
            in_entities = value == "ENTITIES"
            awaiting_section_name = False
            continue

        if in_entities and current_type is not None and code in (10, 20, 11, 21):
            if code in current_values:
                raise ValueError(f"{path}: LINE entity repeats group code {code}")
            current_values[code] = value

    if in_entities:
        finish_entity()
    if not source_lines:
        raise ValueError(f"{path}: no LINE entities found")
    return tuple(source_lines)


def source_bounds(lines: Sequence[SourceLine]) -> tuple[Decimal, Decimal, Decimal, Decimal]:
    xs = [coordinate for line in lines for coordinate in (line.x1, line.x2)]
    ys = [coordinate for line in lines for coordinate in (line.y1, line.y2)]
    return min(xs), min(ys), max(xs), max(ys)


def near(left: Decimal, right: Decimal, tolerance: Decimal) -> bool:
    return abs(left - right) <= tolerance


def is_outer_line(
    line: SourceLine,
    bounds: tuple[Decimal, Decimal, Decimal, Decimal],
    tolerance: Decimal,
) -> bool:
    min_x, min_y, max_x, max_y = bounds
    horizontal = near(line.y1, line.y2, tolerance)
    vertical = near(line.x1, line.x2, tolerance)
    spans_width = (
        (near(line.x1, min_x, tolerance) and near(line.x2, max_x, tolerance))
        or (near(line.x2, min_x, tolerance) and near(line.x1, max_x, tolerance))
    )
    spans_height = (
        (near(line.y1, min_y, tolerance) and near(line.y2, max_y, tolerance))
        or (near(line.y2, min_y, tolerance) and near(line.y1, max_y, tolerance))
    )
    on_horizontal_bound = near(line.y1, min_y, tolerance) or near(line.y1, max_y, tolerance)
    on_vertical_bound = near(line.x1, min_x, tolerance) or near(line.x1, max_x, tolerance)
    return (horizontal and spans_width and on_horizontal_bound) or (
        vertical and spans_height and on_vertical_bound
    )


def round_decimal(value: Decimal) -> int:
    return int(value.to_integral_value(rounding=ROUND_HALF_UP))


def normalize_point(
    x: Decimal,
    y: Decimal,
    bounds: tuple[Decimal, Decimal, Decimal, Decimal],
) -> Point:
    min_x, min_y, max_x, max_y = bounds
    if max_x == min_x or max_y == min_y:
        raise ValueError("DXF outer bounds have zero area")
    normalized_x = (x - min_x) * Decimal(DISPLAY_Q_X) / (max_x - min_x)
    # Display coordinates grow down, so DXF Y must be inverted.
    normalized_y = (max_y - y) * Decimal(DISPLAY_Q_Y) / (max_y - min_y)
    point = Point(round_decimal(normalized_x), round_decimal(normalized_y))
    if not (0 <= point.x <= DISPLAY_Q_X and 0 <= point.y <= DISPLAY_Q_Y):
        raise ValueError(f"normalized point outside display: {point}")
    return point


def normalize_source(path: Path) -> tuple[tuple[Segment, ...], int, int]:
    lines = parse_dxf_lines(path)
    bounds = source_bounds(lines)
    span = max(bounds[2] - bounds[0], bounds[3] - bounds[1])
    tolerance = max(span * Decimal("1e-9"), Decimal("1e-9"))
    outer = [line for line in lines if is_outer_line(line, bounds, tolerance)]
    if len(outer) != 4:
        raise ValueError(f"{path}: expected four complete outer-box LINEs, found {len(outer)}")
    interior = [line for line in lines if not is_outer_line(line, bounds, tolerance)]
    segments = tuple(
        Segment(
            normalize_point(line.x1, line.y1, bounds),
            normalize_point(line.x2, line.y2, bounds),
        )
        for line in interior
    )
    if any(segment.a == segment.b for segment in segments):
        raise ValueError(f"{path}: normalization collapsed an interior LINE")
    if len({segment.canonical() for segment in segments}) != len(segments):
        raise ValueError(f"{path}: duplicate normalized interior LINE")
    return segments, len(lines), len(outer)


def vector_angle_compare(origin: Point, left: Point, right: Point) -> int:
    lx, ly = left.x - origin.x, left.y - origin.y
    rx, ry = right.x - origin.x, right.y - origin.y

    def half(x: int, y: int) -> int:
        return 0 if y < 0 or (y == 0 and x >= 0) else 1

    left_half = half(lx, ly)
    right_half = half(rx, ry)
    if left_half != right_half:
        return -1 if left_half < right_half else 1
    cross = lx * ry - ly * rx
    if cross != 0:
        return -1 if cross > 0 else 1
    left_length = lx * lx + ly * ly
    right_length = rx * rx + ry * ry
    return (left_length > right_length) - (left_length < right_length)


def boundary_edges(vertices: set[Point]) -> set[tuple[Point, Point]]:
    edges: set[tuple[Point, Point]] = set()

    def connect(points: Iterable[Point], key: object) -> None:
        ordered = sorted(set(points), key=key)  # type: ignore[arg-type]
        for first, second in zip(ordered, ordered[1:]):
            edges.add(tuple(sorted((first, second))))  # type: ignore[arg-type]

    connect((point for point in vertices if point.y == 0), lambda point: point.x)
    connect((point for point in vertices if point.y == DISPLAY_Q_Y), lambda point: point.x)
    connect((point for point in vertices if point.x == 0), lambda point: point.y)
    connect((point for point in vertices if point.x == DISPLAY_Q_X), lambda point: point.y)
    return edges


def signed_double_area(polygon: Sequence[Point]) -> int:
    return sum(
        point.x * polygon[(index + 1) % len(polygon)].y
        - polygon[(index + 1) % len(polygon)].x * point.y
        for index, point in enumerate(polygon)
    )


def simplify_polygon(polygon: Sequence[Point]) -> tuple[Point, ...]:
    result = list(polygon)
    changed = True
    while changed and len(result) > 3:
        changed = False
        simplified: list[Point] = []
        for index, point in enumerate(result):
            previous = result[index - 1]
            following = result[(index + 1) % len(result)]
            cross = (point.x - previous.x) * (following.y - point.y) - (
                point.y - previous.y
            ) * (following.x - point.x)
            if cross == 0:
                changed = True
            else:
                simplified.append(point)
        result = simplified
    if len(result) < 3 or signed_double_area(result) == 0:
        raise ValueError("degenerate generated polygon")
    return tuple(result)


def canonicalize_polygon(polygon: Sequence[Point]) -> tuple[Point, ...]:
    result = list(simplify_polygon(polygon))
    # Store every face clockwise in display coordinates.
    if signed_double_area(result) < 0:
        result.reverse()
    start = min(range(len(result)), key=lambda index: (result[index].y, result[index].x))
    return tuple(result[start:] + result[:start])


def build_faces(rules: Sequence[Segment]) -> tuple[tuple[Point, ...], ...]:
    vertices = {Point(0, 0), Point(DISPLAY_Q_X, 0), Point(DISPLAY_Q_X, DISPLAY_Q_Y), Point(0, DISPLAY_Q_Y)}
    for rule in rules:
        vertices.update((rule.a, rule.b))

    edges = boundary_edges(vertices)
    edges.update(rule.canonical() for rule in rules)
    adjacency: dict[Point, list[Point]] = {point: [] for point in vertices}
    for first, second in edges:
        adjacency[first].append(second)
        adjacency[second].append(first)
    for origin, neighbors in adjacency.items():
        neighbors.sort(key=cmp_to_key(lambda left, right: vector_angle_compare(origin, left, right)))

    visited: set[tuple[Point, Point]] = set()
    cycles: list[tuple[Point, ...]] = []
    directed_edges = sorted((first, second) for edge in edges for first, second in (edge, edge[::-1]))
    for start in directed_edges:
        if start in visited:
            continue
        current = start
        cycle: list[Point] = []
        while current not in visited:
            visited.add(current)
            first, second = current
            cycle.append(first)
            neighbors = adjacency[second]
            reverse_index = neighbors.index(first)
            # Take the immediately clockwise edge from the reverse direction.
            following = neighbors[(reverse_index - 1) % len(neighbors)]
            current = (second, following)
        if current != start:
            raise ValueError("DXF geometry produced a non-manifold half-edge traversal")
        cycles.append(canonicalize_polygon(cycle))

    if len(cycles) < 2:
        raise ValueError("DXF geometry did not form any bounded faces")
    outer_index = max(range(len(cycles)), key=lambda index: abs(signed_double_area(cycles[index])))
    faces = tuple(cycle for index, cycle in enumerate(cycles) if index != outer_index)
    expected_area = 2 * DISPLAY_Q_X * DISPLAY_Q_Y
    if sum(abs(signed_double_area(face)) for face in faces) != expected_area:
        raise ValueError("bounded DXF faces do not cover the complete display")
    return faces


def point_on_segment(point: Point, first: Point, second: Point) -> bool:
    cross = (point.x - first.x) * (second.y - first.y) - (
        point.y - first.y
    ) * (second.x - first.x)
    return cross == 0 and min(first.x, second.x) <= point.x <= max(first.x, second.x) and min(
        first.y, second.y
    ) <= point.y <= max(first.y, second.y)


def point_in_polygon(point: Point, polygon: Sequence[Point]) -> bool:
    inside = False
    for index, first in enumerate(polygon):
        second = polygon[(index + 1) % len(polygon)]
        if point_on_segment(point, first, second):
            return True
        if (first.y > point.y) == (second.y > point.y):
            continue
        left = (point.x - first.x) * (second.y - first.y)
        right = (point.y - first.y) * (second.x - first.x)
        if (second.y > first.y and left < right) or (second.y < first.y and left > right):
            inside = not inside
    return inside


def round_fraction(value: Fraction) -> int:
    numerator = value.numerator
    denominator = value.denominator
    if numerator >= 0:
        return (2 * numerator + denominator) // (2 * denominator)
    return -((2 * -numerator + denominator) // (2 * denominator))


def polygon_centroid(polygon: Sequence[Point]) -> Point:
    area_twice = signed_double_area(polygon)
    x_numerator = 0
    y_numerator = 0
    for index, first in enumerate(polygon):
        second = polygon[(index + 1) % len(polygon)]
        cross = first.x * second.y - second.x * first.y
        x_numerator += (first.x + second.x) * cross
        y_numerator += (first.y + second.y) * cross
    center = Point(
        round_fraction(Fraction(x_numerator, 3 * area_twice)),
        round_fraction(Fraction(y_numerator, 3 * area_twice)),
    )
    if not point_in_polygon(center, polygon):
        raise ValueError(f"rounded polygon centroid {center} lies outside its face")
    return center


def assign_slots(spec: LayoutSpec, faces: Sequence[tuple[Point, ...]]) -> tuple[GeneratedSlot, ...]:
    if len(faces) != len(spec.slots):
        raise ValueError(
            f"{spec.source_name}: expected {len(spec.slots)} bounded faces, found {len(faces)}"
        )
    remaining = set(range(len(faces)))
    generated: list[GeneratedSlot] = []
    for slot in spec.slots:
        matches = [index for index in sorted(remaining) if point_in_polygon(slot.anchor, faces[index])]
        if len(matches) != 1:
            raise ValueError(
                f"{spec.source_name}: {slot.position} anchor matched {len(matches)} faces"
            )
        face_index = matches[0]
        remaining.remove(face_index)
        polygon = canonicalize_polygon(faces[face_index])
        generated.append(GeneratedSlot(slot, polygon, polygon_centroid(polygon)))
    if remaining:
        raise ValueError(f"{spec.source_name}: one or more faces have no semantic slot")
    return tuple(generated)


def mirror_segment_x(segment: Segment) -> Segment:
    return Segment(
        Point(DISPLAY_Q_X - segment.a.x, segment.a.y),
        Point(DISPLAY_Q_X - segment.b.x, segment.b.y),
    )


def generate_layouts(repo_root: Path) -> tuple[GeneratedLayout, ...]:
    generated: list[GeneratedLayout] = []
    by_json_id: dict[str, GeneratedLayout] = {}
    for spec in LAYOUT_SPECS:
        source_path = repo_root / "assets" / "dxf" / spec.source_name
        rules, source_count, outer_count = normalize_source(source_path)
        expected_rules = 3 if len(spec.slots) == 3 else 5
        if len(rules) != expected_rules:
            raise ValueError(
                f"{source_path}: expected {expected_rules} interior LINEs, found {len(rules)}"
            )
        if spec.mirror_of is not None:
            reference = by_json_id.get(spec.mirror_of)
            if reference is None:
                raise ValueError(f"mirror source {spec.mirror_of} must be generated first")
            mirrored = {mirror_segment_x(rule).canonical() for rule in reference.rules}
            if mirrored != {rule.canonical() for rule in rules}:
                raise ValueError(
                    f"{source_path}: normalized geometry is not the horizontal mirror of "
                    f"{reference.spec.source_name}"
                )
        layout = GeneratedLayout(
            spec=spec,
            source_sha256=hashlib.sha256(source_path.read_bytes()).hexdigest(),
            source_line_count=source_count,
            outer_line_count=outer_count,
            rules=rules,
            slots=assign_slots(spec, build_faces(rules)),
        )
        generated.append(layout)
        by_json_id[spec.json_id] = layout
    return tuple(generated)


def render_header(layouts: Sequence[GeneratedLayout]) -> str:
    layout_ids = "\n".join(f"    {layout.spec.c_id} = {index}," for index, layout in enumerate(layouts))
    slot_specs = [slot.spec for layout in layouts for slot in layout.slots]
    slot_ids = "\n".join(
        f"    {slot.c_id} = {index}," for index, slot in enumerate(slot_specs, start=1)
    )
    return f"""// Generated by tools/generate_dxf_geometry.py. Do not edit.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern \"C\" {{
#endif

#define PJ_LAYOUT_DISPLAY_WIDTH {DISPLAY_WIDTH}u
#define PJ_LAYOUT_DISPLAY_HEIGHT {DISPLAY_HEIGHT}u
#define PJ_LAYOUT_COORD_SCALE {COORD_SCALE}
#define PJ_LAYOUT_RULE_WIDTH_PX {RULE_WIDTH_PX}u

typedef enum {{
{layout_ids}
    PJ_LAYOUT_COUNT = {len(layouts)},
}} pj_layout_id_t;

typedef enum {{
    PJ_LAYOUT_SLOT_NONE = 0,
{slot_ids}
    PJ_LAYOUT_SLOT_COUNT = {len(slot_specs) + 1},
}} pj_layout_slot_id_t;

typedef struct {{
    int16_t x;
    int16_t y;
}} pj_layout_point_t;

typedef struct {{
    pj_layout_point_t start;
    pj_layout_point_t end;
}} pj_layout_rule_t;

typedef struct {{
    const pj_layout_point_t *vertices;
    uint8_t vertex_count;
}} pj_layout_polygon_t;

typedef struct {{
    pj_layout_slot_id_t id;
    pj_layout_point_t icon_center;
    pj_layout_polygon_t hit_region;
}} pj_layout_slot_t;

typedef struct {{
    pj_layout_id_t id;
    const pj_layout_rule_t *rules;
    const pj_layout_slot_t *slots;
    uint8_t rule_count;
    uint8_t slot_count;
}} pj_layout_geometry_t;

const pj_layout_geometry_t *pj_layout_geometry(pj_layout_id_t layout_id);
pj_layout_slot_id_t pj_layout_hit_test(pj_layout_id_t layout_id, uint16_t x, uint16_t y);
bool pj_layout_pixel_is_rule(pj_layout_id_t layout_id, uint16_t x, uint16_t y);

#ifdef __cplusplus
}}
#endif
"""


def c_point(point: Point) -> str:
    return f"{{{point.x}, {point.y}}}"


def render_source(layouts: Sequence[GeneratedLayout]) -> str:
    declarations: list[str] = []
    geometry_rows: list[str] = []
    for layout in layouts:
        prefix = layout.spec.json_id.upper()
        rules = ",\n".join(
            f"    {{{c_point(rule.a)}, {c_point(rule.b)}}}" for rule in layout.rules
        )
        declarations.append(
            f"static const pj_layout_rule_t {prefix}_RULES[] = {{\n{rules}\n}};"
        )
        slot_rows: list[str] = []
        for index, slot in enumerate(layout.slots):
            polygon_name = f"{prefix}_SLOT_{index}_POLYGON"
            points = ", ".join(c_point(point) for point in slot.polygon)
            declarations.append(
                f"static const pj_layout_point_t {polygon_name}[] = {{{points}}};"
            )
            slot_rows.append(
                "    {"
                f"{slot.spec.c_id}, {c_point(slot.icon_center)}, "
                f"{{{polygon_name}, (uint8_t)(sizeof({polygon_name}) / sizeof({polygon_name}[0]))}}"
                "}"
            )
        declarations.append(
            f"static const pj_layout_slot_t {prefix}_SLOTS[] = {{\n"
            + ",\n".join(slot_rows)
            + "\n};"
        )
        geometry_rows.append(
            "    {"
            f"{layout.spec.c_id}, {prefix}_RULES, {prefix}_SLOTS, "
            f"(uint8_t)(sizeof({prefix}_RULES) / sizeof({prefix}_RULES[0])), "
            f"(uint8_t)(sizeof({prefix}_SLOTS) / sizeof({prefix}_SLOTS[0]))"
            "}"
        )

    hashes = "\n".join(
        f"// {layout.spec.source_name}: sha256 {layout.source_sha256}" for layout in layouts
    )
    return f"""// Generated by tools/generate_dxf_geometry.py. Do not edit.
{hashes}
#include \"pj_layout_geometry.h\"

#include <stdint.h>

{'\n\n'.join(declarations)}

static const pj_layout_geometry_t GEOMETRIES[] = {{
{',\n'.join(geometry_rows)}
}};

static bool point_on_segment(pj_layout_point_t point, pj_layout_point_t first,
                             pj_layout_point_t second)
{{
    int64_t cross = (int64_t)(point.x - first.x) * (second.y - first.y) -
                    (int64_t)(point.y - first.y) * (second.x - first.x);
    return cross == 0 && point.x >= (first.x < second.x ? first.x : second.x) &&
           point.x <= (first.x > second.x ? first.x : second.x) &&
           point.y >= (first.y < second.y ? first.y : second.y) &&
           point.y <= (first.y > second.y ? first.y : second.y);
}}

static bool point_in_polygon(pj_layout_point_t point, const pj_layout_polygon_t *polygon)
{{
    bool inside = false;
    for (uint8_t index = 0; index < polygon->vertex_count; index++) {{
        pj_layout_point_t first = polygon->vertices[index];
        pj_layout_point_t second = polygon->vertices[(uint8_t)((index + 1u) % polygon->vertex_count)];
        if (point_on_segment(point, first, second)) {{
            return true;
        }}
        if ((first.y > point.y) == (second.y > point.y)) {{
            continue;
        }}
        int64_t left = (int64_t)(point.x - first.x) * (second.y - first.y);
        int64_t right = (int64_t)(point.y - first.y) * (second.x - first.x);
        if (((second.y > first.y) && (left < right)) ||
            ((second.y < first.y) && (left > right))) {{
            inside = !inside;
        }}
    }}
    return inside;
}}

static bool point_within_rule(pj_layout_point_t point, const pj_layout_rule_t *rule)
{{
    int64_t vx = rule->end.x - rule->start.x;
    int64_t vy = rule->end.y - rule->start.y;
    int64_t wx = point.x - rule->start.x;
    int64_t wy = point.y - rule->start.y;
    int64_t length_squared = vx * vx + vy * vy;
    int64_t projection = wx * vx + wy * vy;
    const int64_t radius = (PJ_LAYOUT_RULE_WIDTH_PX * PJ_LAYOUT_COORD_SCALE) / 2;
    const int64_t radius_squared = radius * radius;

    if (projection <= 0) {{
        return wx * wx + wy * wy <= radius_squared;
    }}
    if (projection >= length_squared) {{
        int64_t dx = point.x - rule->end.x;
        int64_t dy = point.y - rule->end.y;
        return dx * dx + dy * dy <= radius_squared;
    }}
    int64_t cross = wx * vy - wy * vx;
    return cross * cross <= radius_squared * length_squared;
}}

const pj_layout_geometry_t *pj_layout_geometry(pj_layout_id_t layout_id)
{{
    if ((unsigned)layout_id >= PJ_LAYOUT_COUNT) {{
        return NULL;
    }}
    return &GEOMETRIES[layout_id];
}}

pj_layout_slot_id_t pj_layout_hit_test(pj_layout_id_t layout_id, uint16_t x, uint16_t y)
{{
    const pj_layout_geometry_t *geometry = pj_layout_geometry(layout_id);
    if (geometry == NULL || x >= PJ_LAYOUT_DISPLAY_WIDTH || y >= PJ_LAYOUT_DISPLAY_HEIGHT) {{
        return PJ_LAYOUT_SLOT_NONE;
    }}
    pj_layout_point_t point = {{
        (int16_t)(x * PJ_LAYOUT_COORD_SCALE + PJ_LAYOUT_COORD_SCALE / 2),
        (int16_t)(y * PJ_LAYOUT_COORD_SCALE + PJ_LAYOUT_COORD_SCALE / 2),
    }};
    for (uint8_t index = 0; index < geometry->slot_count; index++) {{
        if (point_in_polygon(point, &geometry->slots[index].hit_region)) {{
            return geometry->slots[index].id;
        }}
    }}
    return PJ_LAYOUT_SLOT_NONE;
}}

bool pj_layout_pixel_is_rule(pj_layout_id_t layout_id, uint16_t x, uint16_t y)
{{
    const pj_layout_geometry_t *geometry = pj_layout_geometry(layout_id);
    if (geometry == NULL || x >= PJ_LAYOUT_DISPLAY_WIDTH || y >= PJ_LAYOUT_DISPLAY_HEIGHT) {{
        return false;
    }}
    pj_layout_point_t point = {{
        (int16_t)(x * PJ_LAYOUT_COORD_SCALE + PJ_LAYOUT_COORD_SCALE / 2),
        (int16_t)(y * PJ_LAYOUT_COORD_SCALE + PJ_LAYOUT_COORD_SCALE / 2),
    }};
    for (uint8_t index = 0; index < geometry->rule_count; index++) {{
        if (point_within_rule(point, &geometry->rules[index])) {{
            return true;
        }}
    }}
    return false;
}}
"""


def point_json(point: Point) -> dict[str, int]:
    return {"x": point.x, "y": point.y}


def render_json(layouts: Sequence[GeneratedLayout]) -> str:
    payload = {
        "schema_version": 1,
        "generator": "tools/generate_dxf_geometry.py",
        "display": {
            "width": DISPLAY_WIDTH,
            "height": DISPLAY_HEIGHT,
            "coordinate_scale": COORD_SCALE,
        },
        "rule_width_px": RULE_WIDTH_PX,
        "boundary_ownership": "first_slot_in_layout_order",
        "layouts": [
            {
                "id": layout.spec.json_id,
                "source": f"assets/dxf/{layout.spec.source_name}",
                "source_sha256": layout.source_sha256,
                "source_line_count": layout.source_line_count,
                "discarded_outer_line_count": layout.outer_line_count,
                **(
                    {"horizontal_mirror_of": layout.spec.mirror_of}
                    if layout.spec.mirror_of is not None
                    else {}
                ),
                "rules": [
                    {"start": point_json(rule.a), "end": point_json(rule.b)}
                    for rule in layout.rules
                ],
                "slots": [
                    {
                        "id": slot.spec.json_id,
                        "position": slot.spec.position,
                        "icon_center": point_json(slot.icon_center),
                        "hit_polygon": [point_json(point) for point in slot.polygon],
                    }
                    for slot in layout.slots
                ],
            }
            for layout in layouts
        ],
    }
    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def generated_outputs(repo_root: Path) -> dict[Path, str]:
    layouts = generate_layouts(repo_root)
    return {
        OUTPUT_PATHS[0]: render_header(layouts),
        OUTPUT_PATHS[1]: render_source(layouts),
        OUTPUT_PATHS[2]: render_json(layouts),
    }


def write_or_check(repo_root: Path, output_root: Path, check: bool) -> bool:
    success = True
    for relative_path, content in generated_outputs(repo_root).items():
        destination = output_root / relative_path
        encoded = content.encode("utf-8")
        if check:
            try:
                existing = destination.read_bytes()
            except FileNotFoundError:
                print(f"missing generated file: {relative_path}", file=sys.stderr)
                success = False
                continue
            if existing != encoded:
                print(f"stale generated file: {relative_path}", file=sys.stderr)
                success = False
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(encoded)
            print(relative_path)
    return success


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if checked-in generated files differ from deterministic output",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        help="write/check outputs below this directory (inputs still come from the repository)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    output_root = args.output_root.resolve() if args.output_root else repo_root
    try:
        return 0 if write_or_check(repo_root, output_root, args.check) else 1
    except (OSError, ValueError) as error:
        print(f"DXF geometry generation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
