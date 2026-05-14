#!/usr/bin/env python3
"""Generate EmotionTriangulation.h from the emotion anchor table.

Reads `FaceConfig::EmotionPoint kEmotionPoints[(size_t)NamedEmotion::Count]`
from robot_v3/src/face/FACE_CONFIG_DATA.h. Each row is `{ v, a }, // Name`
— two floats per named emotion (same coordinates used for discrete snap and
for blend triangulation). Optionally supports legacy four-float rows as an
axis-aligned box; those are converted to box centre `(mid_v, mid_a)`.

Computes a Delaunay triangulation via Bowyer-Watson (no scipy dep). Emits a
C++ header with `kAnchors[]` and `kTriangles[]`, plus JavaScript
`control/scripts/emotion-triangulation.js` (HTML simulator) and TypeScript
`face-editor/src/app/_lib/face-engine/emotionTriangulation.ts` (Next.js face editor).

Duplicate (v, a) coordinates collapse to one anchor (first emotion in file
order wins); matches firmware tie-breaking via `kPickOrder`.

Run by hand whenever kEmotionPoints changes:
    python scripts/gen_emotion_triangulation.py

Sample interior points are checked to lie inside some triangle (coverage
sanity). Domain-corner “inside some box” assertions are not used — anchors are
points, not rectangles.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

DOMAIN_V = (-1.0, 1.0)
DOMAIN_A = (0.0, 1.0)

REPO_ROOT = Path(__file__).resolve().parents[1]
FACE_CONFIG_DATA_H = REPO_ROOT / "robot_v3" / "src" / "face" / "FACE_CONFIG_DATA.h"
OUTPUT_PATH = REPO_ROOT / "robot_v3" / "src" / "behaviour" / "EmotionTriangulation.h"
JS_OUTPUT_PATH = REPO_ROOT / "control" / "scripts" / "emotion-triangulation.js"
TS_OUTPUT_PATH = REPO_ROOT / "face-editor" / "src" / "app" / "_lib" / "face-engine" / "emotionTriangulation.ts"

_KPOINTS_MARKER = "kEmotionPoints[(size_t)EmotionSystem::NamedEmotion::Count]"
_ROW_RE = re.compile(
    r"^\s*\{\s*([^}]+?)\}\s*,?\s*(?://\s*([^\s/]+)\s*)?$"
)

EPS = 1e-9


def parse_emotion_points_from_cpp(path: Path) -> list[tuple[str, float, float]]:
    """Parse FaceConfig::kEmotionPoints initializer: lines from '= {' until '};'.

    Each row: `{ v, a }, // Name` or four floats as legacy box corners.
    """
    if not path.is_file():
        raise SystemExit(
            f"[gen_emotion_triangulation] missing {path}; "
            "cannot read kEmotionPoints."
        )
    lines = path.read_text(encoding="utf-8").splitlines()
    start: int | None = None
    for i, line in enumerate(lines):
        if _KPOINTS_MARKER in line:
            start = i
            break
    if start is None:
        raise SystemExit(
            f"[gen_emotion_triangulation] could not find '{_KPOINTS_MARKER}' "
            f"in {path}"
        )

    j = start
    while j < len(lines) and "= {" not in lines[j]:
        j += 1
    if j >= len(lines):
        raise SystemExit(
            f"[gen_emotion_triangulation] kEmotionPoints declaration has no '= {{' "
            f"in {path}"
        )
    j += 1

    points: list[tuple[str, float, float]] = []
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
                f"[gen_emotion_triangulation] unparseable kEmotionPoints line "
                f"{j + 1} in {path}:\n{lines[j]!r}"
            )
        inner, name = m.group(1), m.group(2)
        if not name:
            raise SystemExit(
                f"[gen_emotion_triangulation] kEmotionPoints line {j + 1} in {path} "
                "needs a // EmotionName comment:\n"
                f"{lines[j]!r}"
            )
        parts = [p.strip() for p in inner.split(",") if p.strip()]
        if len(parts) not in (2, 4):
            raise SystemExit(
                f"[gen_emotion_triangulation] expected 2 or 4 floats in "
                f"kEmotionPoints line {j + 1} in {path}, got {len(parts)}:\n"
                f"{lines[j]!r}"
            )
        try:
            nums = [float(p.rstrip("fF")) for p in parts]
        except ValueError as e:
            raise SystemExit(
                f"[gen_emotion_triangulation] bad float literal in kEmotionPoints "
                f"line {j + 1} in {path}:\n{lines[j]!r}\n{e}"
            ) from e
        if len(nums) == 4:
            v0, a0, v1, a1 = nums
            min_v, max_v = min(v0, v1), max(v0, v1)
            min_a, max_a = min(a0, a1), max(a0, a1)
            v, a = 0.5 * (min_v + max_v), 0.5 * (min_a + max_a)
        else:
            v, a = nums
        points.append((name, v, a))
        j += 1
    else:
        raise SystemExit(
            f"[gen_emotion_triangulation] kEmotionPoints initializer not closed "
            f"with '}};' before EOF in {path}"
        )

    if not points:
        raise SystemExit(
            f"[gen_emotion_triangulation] no kEmotionPoints rows parsed from {path}"
        )
    return points


@dataclass(frozen=True)
class Anchor:
    v: float
    a: float
    emotion: str


def collect_anchors(points: list[tuple[str, float, float]]) -> list[Anchor]:
    """One Anchor per distinct (v, a); first emotion wins duplicates."""
    seen: set[tuple[float, float]] = set()
    out: list[Anchor] = []
    for emotion, v, a in points:
        key = (round(v, 9), round(a, 9))
        if key in seen:
            continue
        seen.add(key)
        out.append(Anchor(v, a, emotion))
    return out


Point = tuple[float, float]
Triangle = tuple[int, int, int]


def circumcircle_contains(pts: list[Point], tri: Triangle, p: Point) -> bool:
    ax, ay = pts[tri[0]]
    bx, by = pts[tri[1]]
    cx, cy = pts[tri[2]]
    px, py = p

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
    min_v = min(p[0] for p in points) - 10.0
    max_v = max(p[0] for p in points) + 10.0
    min_a = min(p[1] for p in points) - 10.0
    max_a = max(p[1] for p in points) + 10.0
    width = max_v - min_v
    height = max_a - min_a
    mid_v = 0.5 * (min_v + max_v)
    super_pts = [
        (mid_v - 20.0 * width, min_a - 1.0),
        (mid_v + 20.0 * width, min_a - 1.0),
        (mid_v, max_a + 20.0 * height),
    ]

    pts = list(points) + super_pts
    s0, s1, s2 = len(points), len(points) + 1, len(points) + 2
    triangles: list[Triangle] = [(s0, s1, s2)]

    for pi, p in enumerate(points):
        bad = [t for t in triangles if circumcircle_contains(pts, t, p)]

        edge_count: dict[tuple[int, int], int] = {}
        for (i, j, k) in bad:
            for e in ((i, j), (j, k), (k, i)):
                key = (min(e), max(e))
                edge_count[key] = edge_count.get(key, 0) + 1
        boundary = [e for e, c in edge_count.items() if c == 1]

        triangles = [t for t in triangles if t not in bad]
        for (i, j) in boundary:
            triangles.append((i, j, pi))

    triangles = [t for t in triangles if all(idx < len(points) for idx in t)]
    return triangles


HEADER_TEMPLATE = """\
// !!! GENERATED FILE - DO NOT EDIT !!!
//
// Generated by scripts/gen_emotion_triangulation.py from FaceConfig::kEmotionPoints
// in robot_v3/src/face/FACE_CONFIG_DATA.h. Re-run that script whenever that table
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


JS_TEMPLATE = """\
// !!! GENERATED FILE - DO NOT EDIT !!!
//
// Generated by scripts/gen_emotion_triangulation.py from FaceConfig::kEmotionPoints
// in robot_v3/src/face/FACE_CONFIG_DATA.h. Re-run that script whenever that table
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


TS_TEMPLATE = """\
import type {{ EmotionTriangulationTable }} from "./types";

// !!! GENERATED FILE - DO NOT EDIT !!!
//
// Generated by scripts/gen_emotion_triangulation.py from FaceConfig::kEmotionPoints
// in robot_v3/src/face/FACE_CONFIG_DATA.h. Re-run that script whenever that table
// changes; this file is the TypeScript sibling of EmotionTriangulation.h and
// must stay in lockstep.
//
// Anchors: {n_anchors}
// Triangles: {n_triangles}

export const EMOTION_TRIANGULATION: EmotionTriangulationTable = {{
  domain: {{ v: [-1.0, 1.0], a: [0.0, 1.0] }},
  anchors: [
{anchor_lines}
  ],
  triangles: [
{triangle_lines}
  ],
}};
"""


def format_ts_anchor(a: Anchor) -> str:
    return (
        f"    {{ v: {a.v:+.6f}, a: {a.a:+.6f}, "
        f"emotion: \"{a.emotion}\" }},"
    )


def format_ts_triangle(t: Triangle) -> str:
    return f"    [{t[0]}, {t[1]}, {t[2]}],"


def _point_in_any_triangle(p: Point, pts: list[Point], tris: list[Triangle]) -> bool:
    for t in tris:
        a, b, c = pts[t[0]], pts[t[1]], pts[t[2]]
        denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denom) < EPS:
            continue
        l1 = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / denom
        l2 = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / denom
        l3 = 1.0 - l1 - l2
        if l1 >= -EPS and l2 >= -EPS and l3 >= -EPS:
            return True
    return False


def main() -> int:
    parsed = parse_emotion_points_from_cpp(FACE_CONFIG_DATA_H)
    anchors = collect_anchors(parsed)

    points: list[Point] = [(a.v, a.a) for a in anchors]
    triangles = bowyer_watson(points)

    sample_pts = [(0.0, 0.5), (-0.5, 0.5), (0.7, 0.55), (0.7, 0.8), (-0.95, 0.5)]
    for sp in sample_pts:
        if not _point_in_any_triangle(sp, points, triangles):
            raise SystemExit(
                f"[gen_emotion_triangulation] sample point {sp} not "
                "covered by any triangle. Triangulation may be incomplete "
                "(collinear anchors / too few distinct points)."
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

    ts_anchor_lines = "\n".join(format_ts_anchor(a) for a in anchors)
    ts_triangle_lines = "\n".join(format_ts_triangle(t) for t in triangles)
    ts_out = TS_TEMPLATE.format(
        n_anchors=len(anchors),
        n_triangles=len(triangles),
        anchor_lines=ts_anchor_lines,
        triangle_lines=ts_triangle_lines,
    )
    TS_OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    TS_OUTPUT_PATH.write_text(ts_out, encoding="ascii")
    print(f"[gen_emotion_triangulation] wrote {TS_OUTPUT_PATH}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
