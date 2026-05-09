# Render pipeline overhaul — Target algebra

This document is the **normative description** of **pipeline B**: how
numeric channels flow from **`FACE_CONFIG.h`** presets to the **vector
renderer**. **`08_DECISIONS.md`** locks forks left ambiguous in earlier
drafts (combine family, `W == 0`, tween placement, overlay split).

## Pipeline diagram

```
 EmotionSystem (v, a) ──► Delaunay triangle + barycentric (l0,l1,l2)
                              │
                              ▼
              strength-weighted blend ──► per-field (value, strength)
              [if W==0: plain barycentric VALUE; strength 0 — see §1]
                              │
                              ▼
              EMOTION-ONLY ~250 ms tween (geometry channels)
                              │
                              ▼
              VerbTimeline sample @ t ──► sparse momentaryOverride[]
                              │
                              ▼
              Combine e + v per field (§3) ──► resolved geometry scalars
                              │
                              ▼
              Idle subsystem (blink / gaze / bob APPLICATION)
              [policy numbers were resolved in same blend+verb math earlier]
                              │
                              ▼
              Vector renderer (semicircular arcs, waves, mood ring)
```

**`EffectsRenderer`** bespoke pixels run **outside** this stack (`08`).

---

## §1 — `ParamI16` and emotion blend

```cpp
struct ParamI16 {
  int16_t value;
  uint8_t strength;  // 0..100
};
```

For each field `f`, triangle anchors **A,B,C** with barycentric weights
**(la, lb, lc)**:

```
w_a = la * A.f.strength
w_b = lb * B.f.strength
w_c = lc * C.f.strength
W   = w_a + w_b + w_c

if W > 0:
  base.f.value    = round((w_a*A.f.value + w_b*B.f.value + w_c*C.f.value) / W)
  base.f.strength = round(la*A.f.strength + lb*B.f.strength + lc*C.f.strength)
else:
  // Locked in 08: legacy plain barycentric of VALUES; strength out = 0
  base.f.value    = round(la*A.f.value + lb*B.f.value + lc*C.f.value)
  base.f.strength = 0
```

**Hull:** outside the convex hull, keep **nearest-anchor** selection for
**values** as today; strength rules follow the same `W` logic on that anchor
set (three copies of one corner → reduces to that corner’s preset).

Barycentric **geometry** (which triangle) does **not** depend on strength.

---

## §2 — Emotion-only tween

~**250 ms** smoothing applies to the **emotion-stage geometry output** only
(before verb timeline sampling). Verbs read the **already smoothed** base
(`08`).

---

## §3 — Verb timelines

While **`VerbSystem`** reports an active verb with a timeline:

```
t = (now_ms - verb_start_ms) % loop_duration_ms
```

For each **`fieldIndex`** present in the timeline, find the two keyframes
that bracket **`t`** (including **wrap across loop end**). Linearly
interpolate **`targetValue`** and **`strength`**.

- **Single keyframe** holds the field for the whole loop.
- **Absent fieldIndex** → verb does **not** override; emotion (+ tween)
  value passes through.

Data structs live in **`02_FACE_CONFIG_H_SPEC.md`**.

---

## §4 — Combine emotion base `e` with verb override `v`

For each field where the verb supplies **`v`**:

- **`final.strength = max(e.strength, v.strength)`** (`08`).
- **`final.value`:** implement a **relative-strength** blend (**2c** family)
  so weak verbs nudge and strong verbs dominate when emotion abstains; if
  edge cases misbehave, **fall back** to the **weighted average** (below).

**Weighted average fallback (2a):**

```
if e.strength + v.strength > 0:
  final.value = round((e.value*e.strength + v.value*v.strength) / (e.strength + v.strength))
else:
  final.value = e.value
  final.strength = 0
```

**Not used:** verb-only `lerp` by `v.strength/100` ignoring **`e.strength`**
(`08` excludes **2b**).

If the verb **omits** a field: **`final = e`**. If **`e.strength == 0`** and
verb abstains: still render **`e.value`** (`08`).

---

## §5 — Idle application (after combine)

**Blink / gaze / draw bob** **policy numbers** are themselves resolved via
the same **`ParamI16` + blend + verb** machinery (`08`). Only **after** §4
does the idle subsystem **mutate** draw state (lid closure, pupil target,
extra `face_y` motion). Exact math is firmware implementation detail but
must be **editor-previewable**.

---

## §6 — What stays below this spec

The **TFT_eSPI** draw pass (arcs, fills, RGB565 push) is unchanged given a
resolved scalar **`FaceParams`**-shaped row + idle adjustments.

---

## History

Older brief text said “verbs are a static `kBaseTargets` row” — **obsolete**.
Verb face = **timelines** only after PR B (`03`).
