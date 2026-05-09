# Face / emotion wiring (Phase 1 PR A)

**Authoritative tables** for expression geometry, V/A anchors, arm blend
presets, stable strings, and mood-ring verb/overlay rules now live in a
single header:

- **`robot_v3/src/face/FACE_CONFIG.h`** (`namespace FaceConfig`)

**Enum contracts** (still edited in firmware today, editor-generated later):

- **`robot_v3/src/face/FaceEnums.h`** — `Face::Expression`, `isEmotionExpression`,
  `EmotionSystem::NamedEmotion`

## Adding or changing an expression / emotion

1. Update **`FaceEnums.h`** if you add enum values (keep **`Count`** last).
2. Append matching rows / entries in **`FACE_CONFIG.h`**:
   - `kBaseTargets[]`
   - `kEmotionPoints[]` + **`kPickOrder[]`** (for named emotions only)
   - `armPresetFor()` table (`kByExpr`)
   - `expressionName` / `emotionName` / `expressionForNamedEmotion` /
     `moodRingEnabledVerbOrOverlay` if applicable
3. Run **`python scripts/gen_emotion_triangulation.py`** from the repo root
   (reads **`FACE_CONFIG.h`**, writes **`EmotionTriangulation.h`** and
   **`control/scripts/emotion-triangulation.js`**).
4. Update **`hal/MotionBehaviors.cpp`** `kMotion[]`, **`SceneContextFill.cpp`**
   **`accentNamedColor()`**, **`Settings`** / **`BridgeControl`** palette keys,
   and the web simulator mirrors **only if** you need them in sync (see
   `EDITOR_BRIEF/08_DECISIONS.md` — simulator sync is optional until the real
   editor exists).

Long-form product brief: **`EDITOR_BRIEF/README.md`** in the monorepo root.

Also read [EMOTION_BLEND.md](EMOTION_BLEND.md) for triangulation behaviour.
