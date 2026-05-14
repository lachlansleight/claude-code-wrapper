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

- **`robot_v3/src/face/FACE_CONFIG_DATA.h`** — `Face::Expression`, `EmotionSystem::NamedEmotion`
- **`robot_v3/src/face/FaceEnums.h`** — `isEmotionExpression()` helper

## Adding or changing an expression / emotion

1. Update **`FACE_CONFIG_DATA.h`** if you add enum values (keep **`Count`** last).
2. Append matching rows in **`FACE_CONFIG_DATA.h`**:
   - `kBaseTargets[]`
   - `kEmotionPoints[]` + **`kPickOrder[]`** (for named emotions only)
   - **`kArmPresets[]`**
3. Update **`FACE_CONFIG.h`** where needed: `expressionName` / `emotionName` /
   `expressionForNamedEmotion` / `moodRingEnabledVerbOrOverlay`.
4. Run **`python scripts/gen_emotion_triangulation.py`** from the repo root
   (reads **`FACE_CONFIG_DATA.h`**, writes **`EmotionTriangulation.h`**,
   **`control/scripts/emotion-triangulation.js`**, and
   **`face-editor/src/app/_lib/face-engine/emotionTriangulation.ts`**).
5. Update **`hal/MotionBehaviors.cpp`** `kMotion[]`, **`SceneContextFill.cpp`**
   **`accentNamedColor()`**, **`Settings`** / **`BridgeControl`** palette keys,
   and the web simulator mirrors **only if** you need them in sync (see
   `EDITOR_BRIEF/08_DECISIONS.md` — simulator sync is optional until the real
   editor exists).

Long-form product brief: **`EDITOR_BRIEF/README.md`** in the monorepo root.

Also read [EMOTION_BLEND.md](EMOTION_BLEND.md) for triangulation behaviour.

### Detailed checklist (emotions, motion, Next.js)

`kEmotionPoints` and `kPickOrder` live in **`FACE_CONFIG_DATA.h`** (not `EmotionSystem.cpp`). When adding a named emotion, extend the same tables as in step 2 above, then run **`python scripts/gen_emotion_triangulation.py`** and commit **`EmotionTriangulation.h`**, **`control/scripts/emotion-triangulation.js`**, and **`face-editor/src/app/_lib/face-engine/emotionTriangulation.ts`**.

For a full step table (FrameController blend vs static row, palette, motion, `face-editor` presets / `EMOTION_COLOR`, and a worked **Frustrated** example), see the expanded sections in git history for this file (commit `371bb529`) — the table assumed `kEmotionPoints` lived in `EmotionSystem.cpp`; this repo keeps anchors in **`FACE_CONFIG_DATA.h`** as described in steps 1–5 above.

### See also

- [`DISPLAY_AND_FACE.md`](DISPLAY_AND_FACE.md) — `FaceParams` field meanings and render path.
- [`EMOTION_BLEND.md`](EMOTION_BLEND.md) — triangulation and hull requirements.
