# Face / emotion wiring (Phase 1 PR A)

**Authoritative numeric tables** (editor-export target) live in:

- **`robot_v3/src/face/FACE_CONFIG_DATA.h`** — `namespace FaceConfig`: `kEmotionPoints`,
  `kPickOrder`, `kBaseTargets`, `kArmPresets`, plus small POD structs (`EmotionPoint`,
  `ArmPreset`).

**Helpers and policy** (hand-maintained until the editor owns more):

- **`robot_v3/src/face/FACE_CONFIG.h`** — includes the data header; `armPresetFor`,
  `expressionForNamedEmotion`, `emotionName`, `expressionName`,
  `moodRingEnabledVerbOrOverlay`.

**Enum contracts** (still edited in firmware today, editor-generated later):

- **`robot_v3/src/face/FaceEnums.h`** — `Face::Expression`, `isEmotionExpression`,
  `EmotionSystem::NamedEmotion`

## Adding or changing an expression / emotion

1. Update **`FaceEnums.h`** if you add enum values (keep **`Count`** last).
2. Append matching rows in **`FACE_CONFIG_DATA.h`**:
   - `kBaseTargets[]`
   - `kEmotionPoints[]` + **`kPickOrder[]`** (for named emotions only)
   - **`kArmPresets[]`**
3. Update **`FACE_CONFIG.h`** where needed: `expressionName` / `emotionName` /
   `expressionForNamedEmotion` / `moodRingEnabledVerbOrOverlay`.
4. Run **`python scripts/gen_emotion_triangulation.py`** from the repo root
   (reads **`FACE_CONFIG_DATA.h`**, writes **`EmotionTriangulation.h`** and
   **`control/scripts/emotion-triangulation.js`**).
5. Update **`hal/MotionBehaviors.cpp`** `kMotion[]`, **`SceneContextFill.cpp`**
   **`accentNamedColor()`**, **`Settings`** / **`BridgeControl`** palette keys,
   and the web simulator mirrors **only if** you need them in sync (see
   `EDITOR_BRIEF/08_DECISIONS.md` — simulator sync is optional until the real
   editor exists).

Long-form product brief: **`EDITOR_BRIEF/README.md`** in the monorepo root.

Also read [EMOTION_BLEND.md](EMOTION_BLEND.md) for triangulation behaviour.
