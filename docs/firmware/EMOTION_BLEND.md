# Emotion blend & triangulation

The base-layer face geometry (before verbs and overlays override it) is
a **continuous barycentric blend** of emotion `FaceParams` presets,
driven by the raw `(valence, activation)` point — not a snap-to-region
lookup.

## How it works

1. `EmotionSystem::kEmotionPoints` lists one `(valence, activation)`
   **anchor** per `NamedEmotion`. Discrete snap uses nearest-anchor
   distance (ties: `kPickOrder`). Blend triangulation uses the same
   coordinates: each anchor is where that emotion's `FaceParams` preset
   applies fully.
2. The anchor cloud is Delaunay-triangulated **offline** by a Python
   script. Output is committed in two places: a C++ header for the
   firmware and a JS sibling for the web simulator.
3. Per-frame, `EmotionBlend::blendedFaceParams(v, a)` linear-scans the
   triangle list, finds the one containing (v, a), and blends the three
   corner emotions' `FaceParams` per-field with barycentric weights.
4. Inside each Delaunay triangle, the three corners are three
   emotions — barycentric blend of their presets. Across the mesh you
   get a continuous piecewise-linear surface in `FaceParams` space.

`SceneContextFill::fill` puts the result on `SceneContext.base_face_params`.
`FrameController` uses it as the tween target whenever
`effective_expression` is an emotion (`Neutral`..`Sad`); verbs and
overlays continue to read directly from `kBaseTargets[expression]`.

## Snap is still alive (for colour & debug)

`EmotionSystem::snapped()` and the snap hysteresis are unchanged. The
mood-ring colour, accent colour, and debug fields still come from the
snapped enum. Only the base `FaceParams` is now blended.

## Coverage sanity check

The generator spot-checks that several interior sample points fall inside
some triangle. If anchors are degenerate (e.g. almost collinear) or too
sparse, triangulation can leave gaps — the script aborts with an error.

## Regenerating after a `kEmotionPoints` change

Edit
[`robot_v3/src/behaviour/EmotionSystem.cpp`](../../robot_v3/src/behaviour/EmotionSystem.cpp)'s
`kEmotionPoints` table only (the Python script parses it directly). Then run:

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
4. Add a `kEmotionPoints` row and `kPickOrder` tie-break if needed. Re-run the
   script. Commit both generated files.
5. Add a palette colour entry if the new emotion needs its own accent
   (`Settings::NamedColor` + `accentNamedColor` + `moodColorForExpression`).

## Performance

Anchor count tracks `NamedEmotion` (duplicate coordinates are merged for
Delaunay). A few dozen triangles is still well under a millisecond per
frame on the S3. If it ever becomes a bottleneck, the upgrade is a
coarse spatial index over triangles.
