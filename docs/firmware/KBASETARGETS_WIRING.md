# Wiring a new `Face::Expression` (and `kBaseTargets` row)

When you add or change a **face expression** that has a row in `FrameController.cpp`’s `kBaseTargets[]`, the row order **must** match `Face::Expression` in `SceneTypes.h`. Several other tables and switches are indexed by that enum or tied to the emotion model. Use this as a checklist so nothing is left half-wired.

Also read [EMOTION_BLEND.md](EMOTION_BLEND.md) for how `(valence, activation)` blending interacts with emotion presets.

---

## 1. Enum and static geometry table

| Step | File | What to do |
|------|------|------------|
| 1a | `robot_v3/src/face/SceneTypes.h` | Add `Expression::YourName` **before** `Count`. Order is the contract for every indexed table. |
| 1b | `robot_v3/src/face/FrameController.cpp` | Append one `FaceParams` initializer to `kBaseTargets[]` in **the same order** as the enum. See the comment above `kBaseTargets` for field order. |

---

## 2. If it is a **named emotion** (V/A regions, idle face when no verb)

Emotions are both a **snap** (`NamedEmotion` + `kEmotionPoints` anchor) and a **blend anchor** (triangulation). Verbs/overlays only use `kBaseTargets` directly.

| Step | File | What to do |
|------|------|------------|
| 2a | `robot_v3/src/behaviour/EmotionSystem.h` | Add `NamedEmotion::YourName` before `Count` (same spirit as `Expression`; order defines `kEmotionPoints` index). |
| 2b | `robot_v3/src/behaviour/EmotionSystem.cpp` | Add a `kEmotionPoints[]` row `{ v, a }` and a `kPickOrder[]` entry (ties in nearest-anchor snap: **earlier** in `kPickOrder` wins). Extend `emotionName()`. |
| 2c | `scripts/gen_emotion_triangulation.py` | Reads `kEmotionPoints` from the same `.cpp` file—no separate table to mirror. |
| 2d | Run | `python scripts/gen_emotion_triangulation.py` — commit **`robot_v3/src/behaviour/EmotionTriangulation.h`** and **`control/scripts/emotion-triangulation.js`**. |
| 2e | `robot_v3/src/behaviour/EmotionBlend.cpp` | Map `NamedEmotion::YourName` → `Face::Expression::YourName` in `expressionForNamedEmotion`. |
| 2f | `robot_v3/src/app/SceneContextFill.cpp` | Same mapping in `expressionForEmotion()`. |

---

## 3. Continuous blend vs static row (`FrameController`)

| Step | File | What to do |
|------|------|------------|
| 3a | `robot_v3/src/face/FrameController.cpp` | If the expression should follow **`SceneContext.base_face_params`** (the V/A blend), include it in **`isEmotionExpression()`**. Verbs and overlays stay **out** of that set—they use the static `kBaseTargets` row only. |
| 3b | `robot_v3/src/face/FrameController.cpp` | **`moodColorForExpression()`** — map to a `Settings::NamedColor` (or existing slot). Mood ring RGB is baked from here for the effective expression. |

Optional polish in the same file: `blinkPeriodMsFor`, `gazeFor`, `bodyBobFor` if the new pose needs distinct motion.

---

## 4. Palette and bridge `setColor`

| Step | File | What to do |
|------|------|------------|
| 4a | `robot_v3/src/hal/Settings.h` | If it needs its own tweakable colour, add `NamedColor::…` before `Count` (append only; do not reorder existing values). |
| 4b | `robot_v3/src/hal/Settings.cpp` | Add a default RGB in `g_defaultColors[]`. |
| 4c | `robot_v3/src/app/SceneContextFill.cpp` | **`accentNamedColor()`** — return the right `NamedColor` for the new `Expression`. |
| 4d | `robot_v3/src/agents/BridgeControl.cpp` | If the Web UI sends string keys, add **`tryParseNamedColor()`** aliases (avoid clashing names, e.g. `emotion_sleepy` vs verb `sleeping`). |

---

## 5. Motion, mood ring visibility, strings

| Step | File | What to do |
|------|------|------------|
| 5a | `robot_v3/src/hal/MotionBehaviors.cpp` | Append one **`kMotion[]`** row per `Expression` (static assert enforces count). |
| 5b | `robot_v3/src/face/MoodRingRenderer.cpp` | **`moodRingEnabledFor()`** — `true` if this expression should draw the ring (match the pattern used for similar moods). |
| 5c | `robot_v3/src/face/SceneTypes.cpp` | **`expressionName()`** — stable snake_case string for debug / logging. |

---

## 6. Web simulator (optional but expected if you use `simulator_v3.html`)

| Step | File | What to do |
|------|------|------------|
| 6a | `control/scripts/frame-controller-v3.js` | Add the name to **`EXPRESSIONS[]`**, **`BASE_TARGETS`**, and any **`motorPeriodMsFor` / `bodyBobFor`** branches that should match the firmware. |
| 6b | `control/simulator_v3.html` | **`EMOTION_COLOR`** — dot colour for blend diagram anchors (optional; falls back to grey if missing). |

---

## 7. Docs and comments

| Step | File | What to do |
|------|------|------------|
| 7a | `docs/firmware/EMOTION_BLEND.md` | Update if you change how many anchors/triangles or the “adding a new emotion” flow. |
| 7b | `docs/README.md` | This guide is linked under **Firmware** in the doc index. |

---

## Quick sanity checks

- `Expression::Count` equals the number of rows in `kBaseTargets` and `MotionBehaviors::kMotion`.
- For emotions: edit `kEmotionPoints` once, then run `gen_emotion_triangulation.py`; regenerate headers/JS after edits.
- `EmotionBlend::expressionForNamedEmotion` and `SceneContextFill::expressionForEmotion` stay in sync.
- New emotion expressions used for idle face belong in **`isEmotionExpression()`** so the face tracks the V/A blend.
- **`kPickOrder`** must list **every** `NamedEmotion` exactly once (tie-break when two anchors are equidistant).

---

## Worked example: `Frustrated`

Adding a named emotion end-to-end (as done for **Frustrated**):

1. **`SceneTypes.h`** — `Expression::Frustrated` before `Count`; extend **`isEmotionExpression()`**.
2. **`FrameController.cpp`** — `kBaseTargets[]` row at the matching index; optional **`blinkPeriodMsFor`** (and similar polish).
3. **`EmotionSystem.h` / `.cpp`** — `NamedEmotion::Frustrated`, **`kEmotionPoints`** row `{ v, a }`, **`kPickOrder`** entry, **`emotionName()`**.
4. **`python scripts/gen_emotion_triangulation.py`** — commit **`EmotionTriangulation.h`** + **`emotion-triangulation.js`**.
5. **`EmotionBlend.cpp`** — `expressionForNamedEmotion` + **`armPresetFor`** for blend arm motion.
6. **`SceneContextFill.cpp`** — `expressionForEmotion` + **`accentNamedColor`** (often a new **`Settings::NamedColor`**).
7. **`Settings.h` / `.cpp`** — **`NamedColor::EmotionFrustrated`** (append-only), default RGB, **bump `kSettingsSchemaVersion`** when the palette gains a slot so NVS reloads cleanly.
8. **`BridgeControl.cpp`** — optional **`tryParseNamedColor`** alias (e.g. `emotion_frustrated`).
9. **`MotionBehaviors.cpp`** — one **`kMotion[]`** row.
10. **`SceneTypes.cpp`** — **`expressionName()`** snake_case string.
11. **`control/scripts/frame-controller-v3.js`** — **`EXPRESSIONS`**, **`BASE_TARGETS`**, **`EMOTION_NAMES`**, **`motorPeriodMsFor`** (and any **`bodyBobFor`** branches).
12. **`simulator_v3.html`** — **`EMOTION_COLOR`** entry for the blend diagram.

---

## See also

- [`DISPLAY_AND_FACE.md`](DISPLAY_AND_FACE.md) — `FaceParams` field meanings and render path.
- [`EMOTION_BLEND.md`](EMOTION_BLEND.md) — triangulation and hull requirements.
