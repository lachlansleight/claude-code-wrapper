# Emotion system

The robot's emotional state is modelled as a single point in a 2D space:
**valence** on the x-axis (negative = unhappy, positive = happy) and
**activation** (or arousal) on the y-axis (low = calm, high = aroused).
Continuous fields make blending well-defined; named emotions like *Happy*
or *Frustrated* are derived from the point's position.

Two files own this:

- `src/behaviour/EmotionSystem.{h,cpp}` — the point and its dynamics.
- `src/behaviour/EmotionBlend.{h,cpp}` — blend `FaceParams` and arm motion
  presets across emotion anchors at the current point.
- `src/behaviour/EmotionTriangulation.h` — generated table of anchors
  and Delaunay triangles.

## Three layers

The system maintains three layered values:

```
goal   ── decays toward held drivers, receives impulses
  │
  │ τ ≈ 300 ms
  ▼
raw    ── continuous (v, a), what renderers read
  │
  │ Voronoi assignment + hysteresis
  ▼
snap   ── nearest NamedEmotion (one of 14)
```

### Goal layer

Receives instantaneous impulses (e.g. `+0.7V, +0.9A` on `turn.ended`) and
is also pulled by **held drivers**. Activation decays back toward 0 with
τ ≈ 6 s; valence relaxes toward the active driver's target with τ ≈ 90 s
(or toward 0 with no driver). The decay constants live in
`FACE_CONFIG_DATA.h`'s `kEmotionSim` block.

### Raw layer

A first-order filter that follows the goal with τ ≈ 300 ms. This is
what `EmotionBlend` reads. It exists so that discrete impulses (a
`turn.ended` adding +0.7 valence in one tick) animate into the geometry
instead of teleporting it.

### Snap layer

The raw point is assigned to the nearest of the 14 `NamedEmotion`
anchors using Euclidean distance in (v, a) space. To prevent flapping
near the Voronoi boundaries:

- The current snap survives until a *meaningfully* closer anchor
  appears (Δdist > 0.05).
- That challenger must remain closer for ≥100 ms before the snap flips.

The snapped name is exposed for diagnostics (`SnappedEmotion`) and used
when the firmware needs a discrete emotion label. **The face geometry
itself does not use the snap** — it uses the continuous blend at the raw
point.

## Held drivers

External systems can pin the goal valence at a particular value while a
condition holds. Each driver has a numeric ID, a target valence, and is
active or inactive. The system has 8 slots; the driver with the largest
`|target|` wins (not the sum). Two are wired up today (`EventRouter`):

| ID | Name              | Target  | When                                      |
|----|-------------------|---------|-------------------------------------------|
| 1  | PendingPermission | -0.6    | `AgentState.pending_permission` non-empty |
| 2  | Straining         | -0.4    | `VerbStraining` has been current ≥ 30 s   |

When all drivers are released, the goal valence relaxes back toward 0.

## Public API

```cpp
namespace EmotionSystem {
  void begin();
  void tick();

  // Instantaneous deltas
  void impulse(float dValence, float dActivation);
  void setValence(float v);
  void setArousal(float a);
  void modifyValence(float dV);
  void modifyArousal(float dA);

  // Held drivers
  void setHeldTarget(uint8_t driverId, float targetV);
  void releaseHeldTarget(uint8_t driverId);

  Emotion         raw();      // (v, a) ∈ [-1,+1] × [0,1]
  SnappedEmotion  snapped();  // raw + nearest NamedEmotion
  DebugState      debugState();
}
```

## Anchor table

The 14 anchors (`FACE_CONFIG_DATA.h::kEmotionPoints`) are placed in
(v, a) space:

| NamedEmotion  |     v |     a |
|---------------|------:|------:|
| Neutral       |   0.0 |   0.5 |
| Happy         |  +0.5 |   0.5 |
| Excited       |  +1.0 |   0.6 |
| Joyful        |  +1.0 |   1.0 |
| Sad           |  -0.5 |   0.5 |
| Sleepy        |  -0.2 |   0.0 |
| Distressed    |  -1.0 |   1.0 |
| Blissed       |  +1.0 |   0.0 |
| Depressed     |  -1.0 |   0.0 |
| Shocked       |  -0.3 |   1.0 |
| Disappointed  |  -1.0 |   0.3 |
| Cheeky        |  +0.5 |   0.7 |
| Gleeful       |  +0.6 |   1.0 |
| Frustrated    |  -0.6 |   0.8 |

These positions, plus the Delaunay triangulation derived from them, are
the input to the blend.

## Triangulation blend

`EmotionTriangulation.h` is **generated** by
`scripts/gen_emotion_triangulation.py` from `kEmotionPoints`. It contains:

- The 14 anchors with their preset `Expression` slot.
- 17 Delaunay triangles, each named by three anchor indices.

At runtime, `EmotionBlend::blendedFaceParams(v, a)`:

1. Finds the triangle that contains `(v, a)`. If the point lies just
   outside the convex hull (which can happen briefly during transitions),
   it uses the nearest triangle and clamps barycentrics.
2. Computes barycentric weights `(la, lb, lc)` for the triangle's three
   anchors.
3. For every `FaceParams` field, calls `blendParam3()`:

```cpp
ParamI16 blendParam3(a, b, c, la, lb, lc) {
    float wa = la * a.strength;
    float wb = lb * b.strength;
    float wc = lc * c.strength;
    float w  = wa + wb + wc;
    if (w <= 0) return ParamI16{0, 0};
    return ParamI16{
        round((a.value*wa + b.value*wb + c.value*wc) / w),
        round(a.strength*la + b.strength*lb + c.strength*lc)
    };
}
```

Strength acts as both a per-anchor weight and survives in the output, so
fields that are "not specified" by an emotion preset (strength 0)
contribute nothing and don't drag the result toward 0.

The same triangle/barycentrics drive two parallel blends:

- `blendedFaceParams(v, a)` → 28 `ParamI16` fields (face geometry, mood ring,
  and arm: `arm_min_deg`, `arm_max_deg`, `arm_period_ms`, `arm_interval_ms`).
- `blendedIdleAnim(v, a)` → blink/gaze/bob amplitude policy; consumed by
  `FrameController` for idle behaviour.

Arm fields are part of `FaceParams`; `MotionBehaviors` reads them from
`Face::effectiveFaceParams()` after smoothing and verb combination, not from
a separate `EmotionArmMotion` preset table.

Inside a rectangular region whose four corners share a NamedEmotion, the
blend collapses to that preset exactly. Between rectangles you get a
smooth gradient.

## Per-frame update (inside `EmotionSystem::tick()`)

1. Apply held-driver pull on `goal.v`: ease toward the active driver's
   target with τ ≈ 90 s.
2. Decay `goal.a` toward 0 with τ ≈ 6 s.
3. Ease `raw` toward `goal` with τ ≈ 300 ms (independent on each axis).
4. Compute distance from `raw` to every anchor.
5. Update the snap with Δdist > 0.05 / ≥100 ms hysteresis.

The output is read by `SceneContextFill`:

- `ctx.mood_v`, `ctx.mood_a` — raw point.
- `ctx.base_face_params` — `EmotionBlend::blendedFaceParams(v, a)` (28 fields).
- `ctx.snapped_emotion` — string name from `snapped()`.
