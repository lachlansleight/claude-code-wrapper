#!/usr/bin/env python3
"""Generate EmotionTriangulation.h from the emotion box table.

Reads constexpr Box kBoxes[(size_t)NamedEmotion::Count] from
robot_v3/src/behaviour/EmotionSystem.cpp. Each row is two opposite corners
(v0,a0), (v1,a1) per the file comment; they are normalized to axis-aligned
min/max before use. For each box, emits its 4 corners
as anchor points tagged with the emotion. Computes a Delaunay triangulation
of the deduplicated anchor set via Bowyer-Watson (no scipy dep). Emits a C++
header with two constexpr arrays: kAnchors[] and kTriangles[].

Run by hand whenever kBoxes changes:
    python scripts/gen_emotion_triangulation.py

Asserts each corner of the (v,a) domain [-1, +1] x [0, 1] lies inside
at least one kBoxes axis-aligned region (not necessarily as a box vertex).
If not, refuses to emit.
"""

from __future__ import annotations

import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

# ---------------------------------------------------------------------------
# kBoxes source (parsed at runtime from EmotionSystem.cpp).
# Each entry: (NamedEmotion name, minV, maxV, minA, maxA) after normalizing
# from corner pair (v0,a0), (v1,a1).
# ---------------------------------------------------------------------------

DOMAIN_V = (-1.0, 1.0)
DOMAIN_A = (0.0, 1.0)

REPO_ROOT = Path(__file__).resolve().parents[1]
EMOTION_SYSTEM_CPP = REPO_ROOT / "robot_v3" / "src" / "behaviour" / "EmotionSystem.cpp"
OUTPUT_PATH = REPO_ROOT / "robot_v3" / "src" / "behaviour" / "EmotionTriangulation.h"
JS_OUTPUT_PATH = REPO_ROOT / "control" / "scripts" / "emotion-triangulation.js"

_KBOXES_MARKER = "kBoxes[(size_t)NamedEmotion::Count]"
_ROW_RE = re.compile(
    r"^\s*\{\s*([^}]+?)\}\s*,?\s*(?://\s*([^\s/]+)\s*)?$"
)

# Tolerance for circumcircle / point-equality tests. Boxes use coords in
# [-1, 1] so 1e-9 is plenty.
EPS = 1e-9


def parse_k_boxes_from_emotion_system_cpp(path: Path) -> list[tuple[str, float, float, float, float]]:
    """Parse kBoxes initializer: lines from '= {' until '};'.

    Each row is `{ v0, a0, v1, a1 }, // Name` — two opposite corners of an
    axis-aligned box (see EmotionSystem.cpp comment on kBoxes). Values are
    normalized to (minV, maxV, minA, maxA) for geometry.
    """
    if not path.is_file():
        raise SystemExit(
            f"[gen_emotion_triangulation] missing {path}; "
            "cannot read kBoxes."
        )
    lines = path.read_text(encoding="utf-8").splitlines()
    start: int | None = None
    for i, line in enumerate(lines):
        if _KBOXES_MARKER in line:
            start = i
            break
    if start is None:
        raise SystemExit(
            f"[gen_emotion_triangulation] could not find '{_KBOXES_MARKER}' "
            f"in {path}"
        )

    j = start
    while j < len(lines) and "= {" not in lines[j]:
        j += 1
    if j >= len(lines):
        raise SystemExit(
            f"[gen_emotion_triangulation] kBoxes declaration has no '= {{' "
            f"in {path}"
        )
    j += 1

    boxes: list[tuple[str, float, float, float, float]] = []
    while j < len(lines):
        stripped = lines[j].strip()
        if stripped.startswith("};"):
            break
        if not stripped or stripped.startswith("//"):
            j += 1
            continue

        m = _ROW_RE.match(lines[j])
        if not m:
            raise SystemExit(
                f"[gen_emotion_triangulation] unparseable kBoxes line "
                f"{j + 1} in {path}:\n{lines[j]!r}"
            )
        inner, name = m.group(1), m.group(2)
        if not name:
            raise SystemExit(
                f"[gen_emotion_triangulation] kBoxes line {j + 1} in {path} "
                "needs a // EmotionName comment:\n"
                f"{lines[j]!r}"
            )
        parts = [p.strip() for p in inner.split(",")]
        if len(parts) != 4:
            raise SystemExit(
                f"[gen_emotion_triangulation] expected 4 floats in kBoxes "
                f"line {j + 1} in {path}, got {len(parts)}:\n{lines[j]!r}"
            )
        try:
            nums = [float(p.rstrip("fF")) for p in parts]
        except ValueError as e:
            raise SystemExit(
                f"[gen_emotion_triangulation] bad float literal in kBoxes "
                f"line {j + 1} in {path}:\n{lines[j]!r}\n{e}"
            ) from e
        v0, a0, v1, a1 = nums
        min_v, max_v = min(v0, v1), max(v0, v1)
        min_a, max_a = min(a0, a1), max(a0, a1)
        boxes.append((name, min_v, max_v, min_a, max_a))
        j += 1
    else:
        raise SystemExit(
            f"[gen_emotion_triangulation] kBoxes initializer not closed "
            f"with '}};' before EOF in {path}"
        )

    if not boxes:
        raise SystemExit(
            f"[gen_emotion_triangulation] no kBoxes rows parsed from {path}"
        )
    return boxes


@dataclass(frozen=True)
class Anchor:
    v: float
    a: float
    emotion: str


def collect_anchors(
    boxes: list[tuple[str, float, float, float, float]],
) -> list[Anchor]:
    seen: dict[tuple[float, float], Anchor] = {}
    for emotion, minV, maxV, minA, maxA in boxes:
        for v in (minV, maxV):
            for a in (minA, maxA):
                key = (round(v, 9), round(a, 9))
                if key not in seen:
                    seen[key] = Anchor(v, a, emotion)
    return list(seen.values())


def _in_closed_box(
    v: float,
    a: float,
    min_v: float,
    max_v: float,
    min_a: float,
    max_a: float,
) -> bool:
    return (
        min_v - EPS <= v <= max_v + EPS
        and min_a - EPS <= a <= max_a + EPS
    )


def assert_domain_corners_pinned_by_boxes(
    boxes: list[tuple[str, float, float, float, float]],
) -> None:
    """Each domain rectangle corner must lie inside some kBoxes region.

    A corner may sit on a box edge or in the interior — it need not be one
    of the four vertex anchors we emit for that box.
    """
    corners = (
        (DOMAIN_V[0], DOMAIN_A[0]),
        (DOMAIN_V[0], DOMAIN_A[1]),
        (DOMAIN_V[1], DOMAIN_A[0]),
        (DOMAIN_V[1], DOMAIN_A[1]),
    )
    missing: list[tuple[float, float]] = []
    for v, a in corners:
        if not any(
            _in_closed_box(v, a, min_v, max_v, min_a, max_a)
            for _name, min_v, max_v, min_a, max_a in boxes
        ):
            missing.append((v, a))
    if missing:
        raise SystemExit(
            "[gen_emotion_triangulation] domain corner(s) not covered by any "
            "kBoxes region: "
            + ", ".join(f"({v}, {a})" for (v, a) in missing)
            + ". Expand or overlap boxes so (-1,0), (-1,1), (1,0), (1,1) "
            "each lie inside at least one axis-aligned box."
        )


# ---------------------------------------------------------------------------
# Bowyer-Watson Delaunay triangulation.
# Reference: https://en.wikipedia.org/wiki/Bowyer%E2%80%93Watson_algorithm
# ---------------------------------------------------------------------------

Point = tuple[float, float]
Triangle = tuple[int, int, int]  # indices into points


def circumcircle_contains(pts: list[Point], tri: Triangle, p: Point) -> bool:
    ax, ay = pts[tri[0]]
    bx, by = pts[tri[1]]
    cx, cy = pts[tri[2]]
    px, py = p

    # Ensure CCW orientation; if not, swap so the in-circle predicate sign works.
    cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
    if cross < 0:
        bx, by, cx, cy = cx, cy, bx, by

    ax_, ay_ = ax - px, ay - py
    bx_, by_ = bx - px, by - py
    cx_, cy_ = cx - px, cy - py

    det = (
        (ax_ * ax_ + ay_ * ay_) * (bx_ * cy_ - cx_ * by_)
        - (bx_ * bx_ + by_ * by_) * (ax_ * cy_ - cx_ * ay_)
        + (cx_ * cx_ + cy_ * cy_) * (ax_ * by_ - bx_ * ay_)
    )
    return det > EPS


def bowyer_watson(points: list[Point]) -> list[Triangle]:
    # Super-triangle large enough to contain all points.
    minV = min(p[0] for p in points) - 10.0
    maxV = max(p[0] for p in points) + 10.0
    minA = min(p[1] for p in points) - 10.0
    maxA = max(p[1] for p in points) + 10.0
    width = maxV - minV
    height = maxA - minA
    midV = 0.5 * (minV + maxV)
    super_pts = [
        (midV - 20.0 * width, minA - 1.0),
        (midV + 20.0 * width, minA - 1.0),
        (midV, maxA + 20.0 * height),
    ]

    pts = list(points) + super_pts
    s0, s1, s2 = len(points), len(points) + 1, len(points) + 2
    triangles: list[Triangle] = [(s0, s1, s2)]

    for pi, p in enumerate(points):
        bad = [t for t in triangles if circumcircle_contains(pts, t, p)]

        # Find the boundary edges of the bad-triangle polygon: edges that
        # appear in exactly one bad triangle.
        edge_count: dict[tuple[int, int], int] = {}
        for (i, j, k) in bad:
            for e in ((i, j), (j, k), (k, i)):
                key = (min(e), max(e))
                edge_count[key] = edge_count.get(key, 0) + 1
        boundary = [e for e, c in edge_count.items() if c == 1]

        # Remove bad triangles, re-triangulate the cavity.
        triangles = [t for t in triangles if t not in bad]
        for (i, j) in boundary:
            triangles.append((i, j, pi))

    # Discard triangles that touch the super-triangle vertices.
    triangles = [t for t in triangles if all(idx < len(points) for idx in t)]
    return triangles


# ---------------------------------------------------------------------------
# Output formatting.
# ---------------------------------------------------------------------------

HEADER_TEMPLATE = """\
// !!! GENERATED FILE - DO NOT EDIT !!!
//
// Generated by scripts/gen_emotion_triangulation.py from the kBoxes
// table in EmotionSystem.cpp. Re-run that script whenever kBoxes
// changes.
//
// Anchors: {n_anchors}
// Triangles: {n_triangles}

#pragma once

#include <Arduino.h>

#include "EmotionSystem.h"

namespace EmotionSystem {{

struct Anchor {{
  float v;
  float a;
  NamedEmotion emotion;
}};

struct Triangle {{
  uint16_t i0;
  uint16_t i1;
  uint16_t i2;
}};

static constexpr size_t kAnchorCount = {n_anchors};
static constexpr Anchor kAnchors[kAnchorCount] = {{
{anchor_lines}
}};

static constexpr size_t kTriangleCount = {n_triangles};
static constexpr Triangle kTriangles[kTriangleCount] = {{
{triangle_lines}
}};

}}  // namespace EmotionSystem
"""


def format_anchor(a: Anchor) -> str:
    return (
        f"    {{ {a.v:+.6f}f, {a.a:+.6f}f, "
        f"NamedEmotion::{a.emotion} }},"
    )


def format_triangle(t: Triangle) -> str:
    return f"    {{ {t[0]}, {t[1]}, {t[2]} }},"


# ---------------------------------------------------------------------------
# JS sibling output. Read by control/simulator_v3.html in blend mode.
# ---------------------------------------------------------------------------

JS_TEMPLATE = """\
// !!! GENERATED FILE - DO NOT EDIT !!!
//
// Generated by scripts/gen_emotion_triangulation.py from the kBoxes
// table in EmotionSystem.cpp. Re-run that script whenever kBoxes
// changes; this file is the JS sibling of EmotionTriangulation.h and
// must stay in lockstep.
//
// Anchors: {n_anchors}
// Triangles: {n_triangles}

window.EmotionTriangulation = {{
  domain: {{ v: [-1.0, 1.0], a: [0.0, 1.0] }},
  anchors: [
{anchor_lines}
  ],
  triangles: [
{triangle_lines}
  ],
}};
"""


def format_js_anchor(a: Anchor) -> str:
    return (
        f"    {{ v: {a.v:+.6f}, a: {a.a:+.6f}, "
        f"emotion: \"{a.emotion}\" }},"
    )


def format_js_triangle(t: Triangle) -> str:
    return f"    [{t[0]}, {t[1]}, {t[2]}],"


def main() -> int:
    boxes = parse_k_boxes_from_emotion_system_cpp(EMOTION_SYSTEM_CPP)
    assert_domain_corners_pinned_by_boxes(boxes)
    anchors = collect_anchors(boxes)

    points: list[Point] = [(a.v, a.a) for a in anchors]
    triangles = bowyer_watson(points)

    # Sanity: every domain-interior point should land in some triangle.
    # Spot-check a handful.
    sample_pts = [(0.0, 0.5), (-0.5, 0.5), (0.7, 0.55), (0.7, 0.8), (-0.95, 0.5)]
    for sp in sample_pts:
        if not _point_in_any_triangle(sp, points, triangles):
            raise SystemExit(
                f"[gen_emotion_triangulation] sample point {sp} not "
                "covered by any triangle. Triangulation is incomplete."
            )

    anchor_lines = "\n".join(format_anchor(a) for a in anchors)
    triangle_lines = "\n".join(format_triangle(t) for t in triangles)
    out = HEADER_TEMPLATE.format(
        n_anchors=len(anchors),
        n_triangles=len(triangles),
        anchor_lines=anchor_lines,
        triangle_lines=triangle_lines,
    )

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(out, encoding="ascii")
    print(
        f"[gen_emotion_triangulation] wrote {OUTPUT_PATH} "
        f"({len(anchors)} anchors, {len(triangles)} triangles)"
    )

    js_anchor_lines = "\n".join(format_js_anchor(a) for a in anchors)
    js_triangle_lines = "\n".join(format_js_triangle(t) for t in triangles)
    js_out = JS_TEMPLATE.format(
        n_anchors=len(anchors),
        n_triangles=len(triangles),
        anchor_lines=js_anchor_lines,
        triangle_lines=js_triangle_lines,
    )
    JS_OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    JS_OUTPUT_PATH.write_text(js_out, encoding="ascii")
    print(f"[gen_emotion_triangulation] wrote {JS_OUTPUT_PATH}")

    return 0


def _point_in_any_triangle(p: Point, pts: list[Point], tris: list[Triangle]) -> bool:
    for t in tris:
        a, b, c = pts[t[0]], pts[t[1]], pts[t[2]]
        # Barycentric.
        denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denom) < EPS:
            continue
        l1 = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / denom
        l2 = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / denom
        l3 = 1.0 - l1 - l2
        if l1 >= -EPS and l2 >= -EPS and l3 >= -EPS:
            return True
    return False


if __name__ == "__main__":
    sys.exit(main())
