# Phase 1 — Firmware plan (stacked PRs)

Work lives in this monorepo under **`robot_v3/`**. Goal: **`FACE_CONFIG.h`**
is the single authored source for expressions, emotions, verb face
timelines, **`kMotion[]`**, and **idle animation policy**; runtime
implements **pipeline B** per [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)
with ordering and forks locked in [`08_DECISIONS.md`](08_DECISIONS.md).

**Not a “data copy only” project** — sub-phase B changes blend math and
verb representation. Hardware **must** be re-checked after B and C.

## PR stack (recommended)

| PR | Name | Goal | Behaviour vs today |
|----|------|------|----------------------|
| **A** | Consolidate | Introduce `FACE_CONFIG.h`, move **current** literals + enums, rewire readers | **Pixel-matched** to pre-refactor firmware |
| **B** | Pipeline overhaul | `ParamI16`, strength blend, `W==0`, emotion-only tween, verb timelines, combine | **Intentional** visual deltas allowed — tune back in header |
| **C** | Motion + idle | `kMotion[]` + idle structs; delete `FrameController` switch polish; EffectsRenderer decouple cleanup | Depends on B |

Merge order: **A → B → C**. Each PR should **compile** and be **flashable**.

---

## PR A — Consolidate (legacy semantics)

### A.1 Create `FACE_CONFIG.h`

- Mechanical lift of **`kBaseTargets[]`**, **`kEmotionPoints[]`**,
  **`kPickOrder[]`**, arm presets, expression/emotion name strings, and
  mapping switches from [`01_CURRENT_STATE.md`](01_CURRENT_STATE.md) /
  firmware — **numeric verbatim**.
- Use **plain `int16_t` `FaceParams`** (or identical layout to today) inside
  this PR if easier; **or** introduce `ParamI16` early with **`strength=100`**
  everywhere — pick whichever minimises churn for A, but B must finish the
  real model.

### A.2 Enums

- Move **`Face::Expression`** and **`NamedEmotion`** definitions into
  **`FACE_CONFIG.h`** (or include-from-single-source pattern per `08`).
- Thin **`SceneTypes.h` / `EmotionSystem.h`** re-export or typedef to
  **`FaceConfig::`**.

### A.3 Rewire consumers to read tables

- **`FrameController.cpp`** — drop local `kBaseTargets`, include config.
- **`EmotionSystem.cpp`** — drop `kEmotionPoints` / `kPickOrder`, read
  **`kEmotions[]`**.
- **`EmotionBlend.cpp`**, **`SceneContextFill.cpp`** — single mapping source.
- **`SceneTypes.cpp`**, **`MoodRingRenderer.cpp`** — forward to config.
- **`accentNamedColor()`** — **leave** in `SceneContextFill.cpp` (firmware
  only, `08`).

### A.4 Python

- Point **`gen_emotion_triangulation.py`** at **`FACE_CONFIG.h`** for anchor
  floats (regex update). Outputs unchanged.

### A.5 Docs

- Replace **`docs/firmware/KBASETARGETS_WIRING.md`** body with a pointer:
  edit **`FACE_CONFIG.h`**, run **`python scripts/gen_emotion_triangulation.py`**.

### Acceptance A

- On-device: **indistinguishable** from pre-work for same agent inputs.
- Triangulation still builds from the new input path.

---

## PR B — Pipeline overhaul

### B.1 Data model

- Emotion presets: **`ParamI16`** everywhere; implement **`W == 0`**
  fallback (`08` / `06`).
- Remove **static verb geometry** from active rendering path — verbs drive
  **`VerbTimeline`** only.
- Implement **emotion-only ~250 ms tween** on the **post-blend emotion
  scalar target** (geometry channels), **before** verb sampling (`08`).

### B.2 Verb engine

- Timeline **sample** + **sparse** `momentaryOverride`.
- **Combine** with emotion output: **primary** relative-strength family
  (**2c**), **`final.strength = max(e,v)`**, **weighted average fallback**
  if needed (`08`).
- Fields absent from override list: pass-through.

### B.3 `SceneContext` / blend plumbing

- Ensure **`SceneContextFill`** / **`EmotionBlend`** supply the pipeline with
  the shapes **`FrameController`** expects after refactor (may split
  “blended emotion pose” vs “final draw pose” structs).

### B.4 Overlays vs effects

- Stop treating **`OverlayWaking` / `OverlayAttention`** as parametric
  **`kBaseTargets`** rows if still present — route overlays through
  **`EffectsRenderer`** per **`08`** (firmware code change; not editor data).

### Acceptance B

- Editor-previewable semantics: emotion strengths, verb sparse overrides,
  and combine behave per **`06` + `08`**.
- On-device sanity pass; file follow-up issues for tuning.

---

## PR C — Motion + idle layer

### C.1 `kMotion[]`

- Delete **`hal/MotionBehaviors.cpp`** table; read **`FaceConfig::kMotion`**.

### C.2 Idle animation **`FACE_CONFIG`** tables

- Replace **`blinkPeriodMsFor` / `gazeFor` / `bodyBobFor`** switches with
  **`FACE_CONFIG.h`** structs (`08` field list).
- Implement subsystem: resolve idle **`ParamI16`** channels through the
  **same** emotion+verb resolver, then **apply** lids / pupil / `face_y` **after**
  verb combine (`08`).

### C.3 Cleanup

- Remove dead code paths, **`static_assert`s** on table sizes, and any
  leftover verb-row reads.

### Acceptance C

- Motion period and idle behaviour are **data-driven** from the header.
- No remaining duplicate per-expression motion tables outside the header.

---

## Simulator (`control/`)

**Keep** `control/simulator_v3.html` and JS as **design reference**; **no**
codegen requirement in Phase 1 (`08`). **Delete** when the real editor ships.

---

## Out of scope (Phase 1)

- Building the Phase 2 **editor app** (separate repo).
- Migrating **`NamedColor`** / palette into the header.
- New bridge messages for **live `FaceParams` push** (preview stays V/A +
  verb per `07`).
