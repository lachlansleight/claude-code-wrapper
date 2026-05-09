# Locked decisions — Phase 1 & pipeline

This file records **implementation choices** agreed in firmware planning
(May 2026). It **supplements** [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)
and [`02_FACE_CONFIG_H_SPEC.md`](02_FACE_CONFIG_H_SPEC.md); where they
differ, **this file wins** for scope and ordering.

---

## Phase 1 end state

- **Single header:** `robot_v3/src/face/FACE_CONFIG.h` is the **authoritative**
  source for expression/emotion data the editor will eventually own.
- **Pipeline B:** not just a mechanical move — firmware implements the
  **full** model from `06`: **`ParamI16`** `(value, strength)` on presets,
  **strength-aware emotion blend**, **verb keyframe timelines** as sparse
  overrides, explicit **combine** rules, then **post-verb** motion layers
  (blink / gaze / bob) as specified below.
- **`scripts/gen_emotion_triangulation.py`** reads anchor geometry from
  **`FACE_CONFIG.h`** (not `EmotionSystem.cpp`) after the refactor.

---

## Execution order inside Phase 1

To avoid editing the same scattered tables twice:

1. **Sub-phase A — Consolidate:** introduce `FACE_CONFIG.h`, move **today’s**
   literals and mappings into it, rewire consumers — **preserve current**
   runtime semantics (plain scalars, current blend, static verb rows) so
   behaviour stays **pixel-matched** on hardware.
2. **Sub-phase B — Overhaul:** migrate that single file + code paths to
   **`ParamI16`**, new blend rules, verb timelines, combine rules, and
   remove static verb geometry rows.
3. **Sub-phase C — Motion & idle layers:** move **`kMotion[]`** into the
   header; add **blink / gaze / bob** authored data and the **final-layer**
   application step.

**Git:** land as **stacked PRs** along those lines (each PR buildable;
prefer no long-lived “two sources of truth” on `main`).

---

## Render pipeline order (runtime)

1. **Emotion** — Delaunay triangle + barycentric weights unchanged in
   **shape**; per-field blend uses **strength-weighted** contribution
   (`06`). When total weight **`W == 0`** for a field: output **`value`**
   using **plain barycentric** blend of the three corner **values** (same
   as legacy); use **outside-hull nearest-anchor** rule where applicable.
   Output **`strength`** for that field can be **0** when no anchor insists.
2. **Emotion-only tween** — approximately **250 ms** smoothing applies **only**
   on this emotion **geometry** trajectory (not on verb output).
3. **Verb timeline** — sample looping timeline → sparse **`momentaryOverride`**
   by `FieldIndex`; fields **absent** from the timeline **do not change**
   the post-emotion values.
4. **Combine** emotion base **`e`** with verb override **`v`** per field
   (see below).
5. **Blink / gaze / bob** — **control parameters** (intervals, Hz, amounts,
   etc.) are resolved through the **same** emotion + verb pipeline as other
   authorable fields; the **subsystem** reads those resolved numbers and
   **only then** mutates the **drawn** face (lids, pupil offset, vertical bob,
   etc.). This is the **final** stage before the existing curve renderer.

**Visual overlays** (non-parametric effects): **`EffectsRenderer`** (or
equivalent) remains **decoupled** from the verb face-blend pipeline — verb
aware, fade in/out, **hardcoded in firmware**, **not** in `FACE_CONFIG.h`,
**not** editor-authored.

---

## Combine rules (emotion base + verb override, per field)

Let **`e`** = emotion-layer output `(value, strength)`, **`v`** = sampled
verb override for that field (if present).

- **`final.value`:** prefer a **relative-strength / 2c-style** rule so the
  verb nudges or dominates based on how its strength compares to emotion’s;
  if that proves awkward at boundaries, **fall back** to the **weighted
  average (2a)** in `06`.
- **Not used:** **2b** (verb-only `lerp` by `v.strength / 100` ignoring
  emotion strength).
- **`final.strength`:** **`max(e.strength, v.strength)`**.

If **verb** has **no** entry for a field: **`final = e`**. If **`e.strength`**
is **0** and verb abstains: still use **`e.value`** for rendering (emotion
value remains the base).

---

## Verbs vs static rows

- Verb **face geometry** is **only** keyframed timelines + sparse overrides.
- **No** legacy static **`kBaseTargets`-style** row that defines the verb’s
  full face; thin **routing metadata** (name, ids) may still exist per verb.

---

## Palette / accent colours

- **`NamedColor`**, **`Settings`** palette, **`accentNamedColor()`**, and
  **`BridgeControl::tryParseNamedColor`** stay **unchanged in firmware** for
  Phase 1. **`ctx.accent_*`** is effectively **debug / TextScene** only today;
  not part of the face editor contract.

---

## `kMotion[]`

- **`kMotion[]`** (body-bob / arm period per expression, today in
  `hal/MotionBehaviors.cpp`) **moves into `FACE_CONFIG.h`** in Phase 1 so
  motion stays keyed to the same **`Expression`** contract as the face.

---

## Blink, gaze, and body bob (authorable)

These are **first-class** rows/structs in **`FACE_CONFIG.h`**, editor-owned
later. Fields (exact C++ layout TBD):

| Subsystem | Parameters |
|-----------|------------|
| **Blink** | Interval **min** and **max** (ms); **duration** (ms). |
| **Bob** | **Frequency** (Hz); **amount** (pixels); **offset** (pixels,
  applied before the wave so oscillation is not necessarily about zero). |
| **Gaze** | **Random amount** ∈ [0, 1] (0 = fixed centre, 1 = fully random);
  **re-roll interval** min/max (ms); **move duration** (ms) to ease the
  pupil toward each new target. |

**Semantics:** these parameters participate in the **same** emotion + verb
**resolution pipeline** as other authorable numbers; **application** to
pixels happens **after** the verb combine step (see pipeline order above).

---

## Simulator (`control/`)

- **Keep** `control/simulator_v3.html` and related JS **for now** as
  **reference** when building the real editor.
- **No** Phase 1 obligation to codegen a JS mirror from `FACE_CONFIG.h`.
- **Remove later** when the editor replaces it.

---

## Open / implementation TBD (not blocking direction)

- **Exact 2c formula** for `final.value` — document in code once prototyped;
  keep **2a** fallback path if needed.
- **Units** for bob “amount” — treat as **pixels** unless hardware tuning
  says otherwise.
- **`Expression` rows for overlays** — face **params** for overlay
  *expressions* may shrink or disappear as effects move to
  `EffectsRenderer`; resolve when Sub-phase B touches `SceneTypes`.

---

## Related brief files

| Doc | Role |
|-----|------|
| [`README.md`](README.md) | Folder index + reading order |
| [`00_OVERVIEW.md`](00_OVERVIEW.md) | Product overview |
| [`01_CURRENT_STATE.md`](01_CURRENT_STATE.md) | Legacy inventory + bootstrap literals |
| [`02_FACE_CONFIG_H_SPEC.md`](02_FACE_CONFIG_H_SPEC.md) | Target `FACE_CONFIG.h` layout |
| [`03_FIRMWARE_REFACTOR.md`](03_FIRMWARE_REFACTOR.md) | Stacked PR plan (A / B / C) |
| [`04_EDITOR_REQUIREMENTS.md`](04_EDITOR_REQUIREMENTS.md) | Phase 2 editor |
| [`05_FACE_RENDER_REFERENCE.md`](05_FACE_RENDER_REFERENCE.md) | Curve / pixel visual contract |
| [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md) | Resolver algebra (normative) |
| [`07_BRIDGE_INTEGRATION.md`](07_BRIDGE_INTEGRATION.md) | Bridge preview |

**Normative hierarchy:** geometry math → **`06`**. Scope / ordering forks →
**`08`** (this file). If **`06`** still shows a draft formula that **`08`**
supersedes (e.g. combine **2c** vs **2a** fallback), implement **`08`**.
