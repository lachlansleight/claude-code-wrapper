# Emotion Blend (Barycentric) — Implementation Plan

## Goal

Replace the current "snap to one emotion region → look up that region's
`FaceParams` preset" base-layer behaviour with a **continuous
barycentric blend** of nearby emotion presets, driven by the raw
(valence, activation) point.

Verbs and overlays continue to override the base layer exactly as today.
The snapped `NamedEmotion` continues to drive colour and debug fields.

## Why

Today the face geometry pops when the (v,a) point crosses a rect
boundary — even with hysteresis, the underlying preset is a step
function. We want geometry to vary smoothly across the plane so the
face quietly drifts as mood drifts, and so emotion regions stacked along
the same valence column (Happy → Excited → Joyful) interpolate into one
another as activation rises.

## Mental model

- Each emotion is defined by a rectangle in (v,a). The four corners of
  that rectangle are **anchor points** at which the emotion's
  `FaceParams` preset is fully expressed.
- Inside the rectangle, all four corners belong to the same emotion, so
  the blend is 100% that emotion (consistent within the box).
- Between rectangles, a Delaunay triangulation of all anchor points
  gives a unique triangle for any (v,a). Barycentric weights inside that
  triangle blend the three corners' emotion presets per-field.
- Two stacked rectangles that share a valence column (e.g. Happy at
  a∈[0,0.5] and Excited at a∈[0.6,0.7]) produce a smooth interpolation
  band between them, controlled by activation.

### Hard requirement

The convex hull of all anchor points must cover the full
`[-1, +1] × [0, 1]` (v, a) domain. Today this holds because Sad's
corners include (-1,0) and (-1,1), Happy's include (1,0), and Joyful's
include (1,1). The script must assert this and refuse to emit the
header otherwise.

## Pieces

### 1. Triangulation generator script

**Path**: `scripts/gen_emotion_triangulation.py`

**Language**: Python 3 with `scipy.spatial.Delaunay` and `numpy`. Chosen
over TS because scipy makes Delaunay a one-liner; the alternative is
hand-rolling Bowyer-Watson or pulling in a JS lib. Script is dev-only
and run by hand when boxes change.

**Inputs**: a duplicated copy of the `kBoxes` table from
`robot_v3/src/behaviour/EmotionSystem.cpp`, hard-coded near the top of
the script. The box list is short and changes rarely; sharing it across
language boundaries isn't worth the complexity right now. (Future:
extract into a JSON file consumed by both sides.)

**Behaviour**:
1. Build the anchor list: for each box, emit its 4 corners tagged with
   the `NamedEmotion`. **Deduplicate** anchors that share an exact
   (v, a) — keep only the first encountered, since downstream blending
   only needs one anchor per unique point. (If duplicates didn't get
   merged, scipy would still triangulate but we'd have ambiguous
   per-anchor emotions at the duplicate location.) When a few dozen
   rects exist, dedup will become more important.
2. Assert the convex hull of the anchors contains the four screen
   corners: (-1,0), (-1,1), (+1,0), (+1,1). Abort with a clear error
   listing which corners are uncovered if not.
3. Run `scipy.spatial.Delaunay` on the anchor points.
4. Emit `robot_v3/src/behaviour/EmotionTriangulation.h` containing:
   - `constexpr size_t kAnchorCount = N;`
   - `constexpr Anchor kAnchors[N] = { {v, a, NamedEmotion::X}, ... };`
   - `constexpr size_t kTriangleCount = T;`
   - `constexpr Triangle kTriangles[T] = { {i0, i1, i2}, ... };`
   - Generated-by-script header comment with the script path and a
     warning not to hand-edit.

**Output struct definitions** live in
`robot_v3/src/behaviour/EmotionTriangulation.h` itself (next to the
constexpr arrays), not in a separate header — keeps the
generated/regenerated unit self-contained.

```cpp
struct Anchor {
  float v;
  float a;
  EmotionSystem::NamedEmotion emotion;
};
struct Triangle {
  uint16_t i0, i1, i2;  // indices into kAnchors
};
```

### 2. Runtime blender

**Path**: new file `robot_v3/src/behaviour/EmotionBlend.{h,cpp}`. Lives
in `behaviour/` because it consumes `EmotionSystem::NamedEmotion` and
the `kBaseTargets` table from `face/`. It produces a `Face::FaceParams`,
so it depends on `face/SceneTypes.h` and the
`FrameController::kBaseTargets` table. (May need to expose
`kBaseTargets` via the header — currently it's `static const` in
`FrameController.cpp`. Plan: move it to a small accessor in
`FrameController.h` like `const FaceParams& baseTargetFor(Expression)`,
or expose the array directly. Will pick the lighter touch when I'm in
the file.)

**Public API**:

```cpp
namespace EmotionBlend {
Face::FaceParams blendedFaceParams(float v, float a);
}
```

**Algorithm per call**:
1. Linear-scan `kTriangles`. For each triangle, compute barycentric
   weights of (v,a) using the standard 2D formula. If all three λ ≥
   −ε (tiny tolerance for floating-point edge cases), this is our
   triangle.
2. Clamp negative λs to 0 and renormalise so Σλ = 1 (handles edge cases
   where (v,a) is exactly on a triangle edge or just outside due to
   floating-point error).
3. For each `int16_t` field of `FaceParams`, compute Σ λᵢ ·
   `kBaseTargets[anchor[i].emotion].field`, round to int16. Includes
   `ring_r/g/b` — those will blend smoothly too, which is fine because
   the colour layer doesn't read them on this path (the snap-based
   accent colour wins for the visible tint).
4. If no triangle contains the point (shouldn't happen because the hull
   covers the domain — but defensively): fall back to the nearest
   anchor's preset.

**Performance**: ~30 triangles today, ~200 if the rect set grows to a
few dozen. Linear scan with ~10 fp ops per triangle is sub-millisecond
on the S3. If profiling ever shows it as a bottleneck, the upgrade is a
4×4 spatial bucket index over triangles (each triangle registered in
every grid cell its bbox touches) — `O(1)` lookup. Not doing that now.

### 3. Wiring into the scene fill

**File**: `robot_v3/src/app/SceneContextFill.cpp`.

In `fill()`, the current code resolves `out.effective_expression` from
either the verb (overlay/active) or the snapped emotion. The verb
branches are unchanged. The emotion branch keeps setting
`out.effective_expression = expressionForEmotion(snapped().named)` so
that downstream (colour, debug, expression-keyed tables) sees a single
emotion.

**New addition**: alongside `effective_expression`, populate a new
`SceneContext` field — `Face::FaceParams base_face_params` — with the
blended params. This requires a new field on `SceneContext` in
`SceneTypes.h`.

**Downstream consumer**: `FrameController` currently calls
`kBaseTargets[expression]` to get the tween target. We change it so
that when the active expression is an *emotion* (Neutral..Sad), it uses
`ctx.base_face_params` instead of `kBaseTargets[expression]`. For verb
and overlay expressions it keeps the table lookup so verbs override
cleanly. The tween from the previous frame's params into the new
blended params already exists via `FrameController`'s smoothing, so the
blend is double-smoothed (low-pass on (v,a) inside `EmotionSystem`,
plus the frame-controller tween). That's intentional and matches what
we discussed.

I'll need to read `FrameController.cpp` first to confirm the exact tween
path and pick the cleanest patch point. Will adjust the plan if I find
something unexpected there.

## Things explicitly NOT changing

- `EmotionSystem::snapped()` and the snap hysteresis. Still used for
  colour, debug fields, and the verb/overlay branch in
  `SceneContextFill`.
- Verb and overlay overrides — they continue to point to a single
  `Expression` enum and use `kBaseTargets[expression]`.
- The (v,a) low-pass time constants. Smoothness comes from those.
- The mood-ring colour pipeline.
- The `Expression` enum or its ordering. Adding new emotions in future
  means adding to both `NamedEmotion` and `Expression` (already
  parallel today).

## Edge cases / risks

- **Convex hull regression**: if someone shrinks the Sad rect or moves
  Joyful inward, the hull may stop covering the domain. The script
  asserts this, so the failure mode is "header doesn't regenerate"
  rather than "robot crashes."
- **Anchor dedup with future rects**: if two rects share a corner
  exactly, the dedup keeps only one (with the first emotion in box
  order). That means the corner's preset is biased toward whichever
  emotion comes first in `kBoxes`. If this ever feels wrong, the fix
  is to perturb one box by a tiny amount — keeping shared corners
  visually-but-not-numerically equal.
- **`kBaseTargets` visibility**: currently `static` in
  `FrameController.cpp`. Will need to expose it (as an array reference
  or accessor) from `FrameController.h` for `EmotionBlend` to read. No
  behaviour change.
- **`int16_t` rounding**: blending integer fields with `lroundf` is
  fine; sub-pixel jitter from blend coefficients is below the
  frame-controller's own tween granularity.
- **Held drivers and target-snap-by-name**: held drivers only target
  valence today, not activation. The blend doesn't change that — it's
  still the (v,a) point that gets blended.

## Order of work

1. Read `FrameController.cpp` to confirm tween wiring and the cleanest
   exposure of `kBaseTargets`.
2. Write `scripts/gen_emotion_triangulation.py`. Run it; verify the
   generated header looks reasonable (anchor count = 20 for current
   rects, triangle count ≈ 30, hull check passes).
3. Hand-check the generated triangulation by sketching it out — make
   sure no weird thin sliver triangles span big distances unexpectedly.
4. Write `EmotionBlend.{h,cpp}`. Unit-test mentally with a few sample
   points: each rect's interior should produce that emotion's preset
   exactly; midpoint between Happy and Excited should produce a 50/50
   blend along an activation gradient.
5. Add `base_face_params` to `SceneContext`, populate in
   `SceneContextFill::fill`.
6. Update `FrameController` to consume `ctx.base_face_params` for
   emotion expressions; verb/overlay path unchanged.
7. Build the firmware (`robot_v3` Arduino compile) to catch link/type
   errors. **Cannot test on hardware in this session** — call this out
   when reporting back.

## Resumption notes (if interrupted)

If picking this up cold:
- The high-level direction is settled and confirmed by Lachlan: per-rect
  4-corner anchors, Delaunay triangulation, barycentric per-field blend,
  generated by a Python script run by hand.
- Don't reopen the snap-vs-blend or IDW-vs-barycentric questions — both
  decided.
- Verbs/overlays/colour stay snap-based.
- The next concrete file to read before any edits is
  `robot_v3/src/face/FrameController.cpp` to confirm the tween wiring.
- This file (`docs/ideas/NEW_BLEND_IMPLEMENTATION_PLAN.md`) is the spec;
  treat deviations as needing user sign-off.
