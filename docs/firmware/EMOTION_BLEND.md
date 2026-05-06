# Emotion blend & triangulation

The base-layer face geometry (before verbs and overlays override it) is
a **continuous barycentric blend** of emotion `FaceParams` presets,
driven by the raw `(valence, activation)` point — not a snap-to-region
lookup.

## How it works

1. `EmotionSystem::kBoxes` partitions the (v, a) plane into one
   axis-aligned rectangle per `NamedEmotion`. The four corners of each
   box are **anchor points** at which that emotion's preset (the
   matching row of `Face::baseTargetFor`) is fully expressed.
2. The anchor cloud is Delaunay-triangulated **offline** by a Python
   script. Output is committed in two places: a C++ header for the
   firmware and a JS sibling for the web simulator.
3. Per-frame, `EmotionBlend::blendedFaceParams(v, a)` linear-scans the
   triangle list, finds the one containing (v, a), and blends the three
   corner emotions' `FaceParams` per-field with barycentric weights.
4. Inside any rectangle, all four corners share the same emotion → the
   blend collapses to that emotion's preset exactly. Between rectangles
   you get a smooth gradient.

`SceneContextFill::fill` puts the result on `SceneContext.base_face_params`.
`FrameController` uses it as the tween target whenever
`effective_expression` is an emotion (`Neutral`..`Sad`); verbs and
overlays continue to read directly from `kBaseTargets[expression]`.

## Snap is still alive (for colour & debug)

`EmotionSystem::snapped()` and the snap hysteresis are unchanged. The
mood-ring colour, accent colour, and debug fields still come from the
snapped enum. Only the base `FaceParams` is now blended.

## Hard requirement: hull covers the domain

The convex hull of all anchors **must** cover `[-1, +1] × [0, 1]`. The
generator script enforces this by checking that the four extreme
corners — `(-1, 0)`, `(-1, 1)`, `(+1, 0)`, `(+1, 1)` — are anchors. If
you change `kBoxes` such that a corner is missing, the script aborts
with an error rather than emitting a header.

## Regenerating after a `kBoxes` change

When you edit
[`robot_v3/src/behaviour/EmotionSystem.cpp`](../../robot_v3/src/behaviour/EmotionSystem.cpp)'s
`kBoxes` table, mirror the same edit in the `BOXES` list at the top of
[`scripts/gen_emotion_triangulation.py`](../../scripts/gen_emotion_triangulation.py),
then run:

```
python scripts/gen_emotion_triangulation.py
```

That writes two files (commit both):

- `robot_v3/src/behaviour/EmotionTriangulation.h` — consumed by
  `EmotionBlend.cpp`.
- `control/scripts/emotion-triangulation.js` — consumed by
  `control/simulator_v3.html` so the simulator's blend matches the
  device.

Both are marked `GENERATED FILE - DO NOT EDIT` at the top. No scipy
needed; the script ships its own Bowyer-Watson.

## Adding a new emotion

1. Add a `NamedEmotion::Foo` value in `EmotionSystem.h` (before
   `Count`).
2. Add a parallel `Face::Expression::Foo` row in `SceneTypes.h` and a
   `FaceParams` row at the same index in
   `FrameController::kBaseTargets`. **Order must match.**
3. Update the `expressionForNamedEmotion` switch in
   `EmotionBlend.cpp` and the parallel one in
   `SceneContextFill::expressionForEmotion`.
4. Add a `kBoxes` entry and a `BOXES` entry in the script. Re-run the
   script. Commit both generated files.
5. Add a palette colour entry if the new emotion needs its own accent
   (`Settings::NamedColor` + `accentNamedColor` + `moodColorForExpression`).

## Performance

Today: 20 anchors, 24 triangles. A few dozen rectangles would push it
to ~200 triangles, still well under a millisecond per frame on the
S3. If it ever becomes a bottleneck, the upgrade is a coarse spatial
bucket index over triangles.
