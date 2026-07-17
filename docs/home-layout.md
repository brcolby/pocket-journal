# Fixed DXF-backed launcher layouts

The launchers are product-owned geometry, not user data. They have no arbitrary
one-to-four-slot model, focus order, timeout, custom destination string, storage
record, or LAN/USB-C mutation API. Changing a launcher is a firmware and product
design change.

The Home launcher uses `assets/dxf/3_1.dxf`, normalized to the complete 200 x
200 display with DXF Y inverted. Its three fixed sectors are:

| Position | Destination | Carbon graphic |
| --- | --- | --- |
| Left | Time | `TimeFilled` |
| Bottom | Notes | `DataEnrichment` |
| Right | Settings | `ServiceLevels` |

The same generated geometry contract covers the other top-level launchers:

| Launcher | DXF | Fixed sector order |
| --- | --- | --- |
| Notes | `3_1m.dxf` | Record (top), Listen (left), Read (right) |
| Time | `4_1.dxf` | Alarm (top-left), Stopwatch (top-right), Timer (bottom-left), Interval (bottom-right) |
| Settings | `4_0m.dxf` | Volume (top-left), Theme (top-right), 12H/24H (bottom-left), Sync (bottom-right) |

`4_1.dxf` is the canonical unmirrored four-sector source. `4_0m.dxf` is
validated as its exact horizontal geometry mirror. Slot identities are assigned
after the geometry transform, so the rules may mirror without mirroring Carbon
icons or changing their meanings.

`tools/generate_dxf_geometry.py` uses only the Python standard library. It
parses `LINE` entities, finds and discards exactly the four complete outer-box
entities, normalizes the remaining segments, and preserves every interior rule,
including the short four-sector center diagonal. The checked-in outputs are the
typed firmware geometry and matching simulator JSON. Run:

```bash
python3 tools/generate_dxf_geometry.py
python3 tools/generate_dxf_geometry.py --check
```

Interior rules use the shared 4 px raster token; the display edge is not drawn
again. The generator derives closed hit polygons and icon centroids from those
same normalized segments. Touch coordinates sample pixel centers, every one of
the 40,000 display pixels belongs to at least one sector, and a point exactly on
a shared boundary belongs to the first slot in the documented layout order.
That ordering makes centerline ownership deterministic across firmware, native
tests, and the simulator.
