# Face editor — `FACE_CONFIG_DATA` coverage

Context for future editor work. Describes what lives in the session
`FaceConfigState` (mirrored in generated
[`FACE_CONFIG_DATA.ts`](src/app/_lib/face-engine/FACE_CONFIG_DATA.ts) and
[`robot_v3/src/face/FACE_CONFIG_DATA.h`](../robot_v3/src/face/FACE_CONFIG_DATA.h)),
what the UI can change today, and what is save-only / hand-edited.

**Load path:** `GET /api/loadFaceConfig` → snapshot JSON if present, else shipped
`FACE_CONFIG_DATA.ts` + Delaunay from `kEmotionPoints`.

**Save path:** **Save Face Config Data** → `POST /api/saveData` → regenerates
`.ts`, `.h`, `EmotionTriangulation.h`, and `.snapshot.json`.

**Schema v3:** each `kBaseTargets` row is **28** `ParamI16` cells — face geometry,
mood ring, and four **arm** fields (`arm_min_deg`, `arm_max_deg`,
`arm_period_ms`, `arm_interval_ms`). Legacy `kArmPresets`, `kMotion`, and
`kMotionRuntime` tables were removed; arm timing is always milliseconds.

---

## 1. High-level map of `FaceConfigState`

| Section                 | Role                                                                                      |
| ----------------------- | ----------------------------------------------------------------------------------------- |
| **Expression schema**   | Fixed list of 22 `Face::Expression` values, which are “emotions” vs verbs/overlays        |
| **Named emotions (14)** | V/A anchors, pick order, mapping to expression indices                                    |
| **Triangulation**       | Delaunay mesh over emotion anchors (computed at load/save, not stored as source of truth) |
| **`kBaseTargets`**      | 22 × 28 `ParamI16` rows (face + ring + arm; value + strength per field)                   |
| **`kVerbTimelines`**    | Per-verb keyframed override tracks (time → partial field overrides, including arm)        |
| **`kIdleAnim`**         | Per-expression blink, gaze, and face-bob **amplitude** policy (not arm period)          |
| **Sim tunables**        | `kEmotionSim`, `kFrameAnim`, `kVerbSim`, `kVerbTransitionDurMs`                         |

Types and helpers live in [`faceConfigTypes.ts`](src/app/_lib/face-engine/faceConfigTypes.ts)
(enums, `P()`, `expressionIndexFromName`, etc.). Generated files are **data only**.

---

## 2. Edited in the UI (today)

### Emotion V/A map — `kEmotionPoints` + triangulation

| What                                        | UI location                                                                                                                         |
| ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Anchor positions `(v, a)` per named emotion | **Emotions** mode → blend diagram canvas ([`BlendPanel.tsx`](src/app/_components/face-editor/BlendPanel.tsx)): drag emotion anchors |
| Triangle mesh                               | Recomputed automatically (`delaunator`) on drag and before save                                                                     |

`emotionPoints` and `emotionTriangulation.anchors` stay in sync when anchors move
([`syncEmotionPointFromAnchor`](src/app/_lib/face-engine/emotionTriangulationLive.ts)).

**Not in UI:** renaming emotions, adding/removing emotions, `kPickOrderIndices`,
`kNamedEmotionToExpressionIndex` (fixed for now; schema editor coming later).

---

### Emotion face + arm geometry — `kBaseTargets` (14 emotion rows)

| What                                                     | UI location                                                                                                                                                      |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-field **value** and **strength** for a named emotion | **Emotions** mode → click anchor on blend diagram → [`EmotionPointInspector.tsx`](src/app/_components/face-editor/EmotionPointInspector.tsx) (`ParamSliderGrid`) |
| Arm min/max (°), period/interval (ms)                    | Same inspector — **Arm** section at bottom of `ParamSliderGrid`                                                                                                  |
| Valence / arousal readout                                | Top of `EmotionPointInspector` (from live anchor)                                                                                                                |

Rows addressed by **PascalCase** emotion name (`Neutral`, `Happy`, …) matching
triangulation anchors — not by expression index dropdown.

Arm fields barycentric-blend with the rest of the face when V/A moves between
anchors ([`emotionBlend.ts`](src/app/_lib/face-engine/emotionBlend.ts)).

**Not in UI:** direct editor for verb/overlay rows in `kBaseTargets` (see §3).

---

### Verb face + arm animation — `kVerbTimelines`

| What                                             | UI location                                                                                                                                                                          |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Keyframe times, override fields/values/strengths | **Verbs** mode → [`VerbTimelinePanel.tsx`](src/app/_components/face-editor/VerbTimelinePanel.tsx) + [`KeyframeInspector.tsx`](src/app/_components/face-editor/KeyframeInspector.tsx) |
| Arm overrides (same four fields as emotions)     | Keyframe inspector — add `arm_*` fields like any other param                                                                                                                         |
| Loop duration                                    | Verb timeline panel                                                                                                                                                                  |
| Preview playhead / speed                         | Verb timeline panel + [`FaceStage`](src/app/_components/face-editor/FaceStage.tsx) canvas                                                                                            |

Applies to expressions that use verb timelines: `VerbThinking`, `VerbReading`,
`VerbWriting`, `VerbExecuting`, `VerbStraining`, `VerbSleeping`.

Shipped verb rows in `FACE_CONFIG_DATA` use `FACE_ROW_EMPTY` (all zeros including
arm); tune arm per verb in the timeline or by editing generated data.

---

### Preview-only (does **not** write config)

| What                   | UI location                                                                                          | Notes                                                                              |
| ---------------------- | ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Blend sample point V/A | **Emotions** mode → valence/arousal sliders in `BlendPanel`                                          | Moves preview only; does not update `kEmotionPoints` unless you drag an **anchor** |
| Static face override   | **Static mode** panel ([`StaticModePanel.tsx`](src/app/_components/face-editor/StaticModePanel.tsx)) | `staticOverride` for preview + C++ copy snippet; does **not** patch `kBaseTargets` |
| Emotion quick buttons  | [`EmotionButtons.tsx`](src/app/_components/face-editor/EmotionButtons.tsx)                           | Sends V/A to robot bridge; does not open inspector or edit stored tables           |
| Robot bridge overlays  | Overlay buttons, auto-send, etc.                                                                     | Runtime only                                                                       |

---

## 3. Not edited in the UI (today)

These are loaded into session state, included in **Save**, and round-trip unchanged
unless you edit the generated files by hand.

### Schema (fixed until emotion-schema work lands)

-   `expressions` / `EXPRESSIONS` (22 names)
-   `expressionIsEmotion` (emotion vs verb/overlay flags)
-   `emotionNames` (14 slugs — positions editable, names not)
-   `verbKeyframeOverridesMax`, `verbKeyframesMax`
-   `bobAmpFollowEmotionArm` (sentinel `0x8000` for “follow arm” bob amplitude)

### Emotion metadata

-   `pickOrderIndices` — tie-break when two anchors are equidistant
-   `namedEmotionToExpressionIndex` — maps each named emotion → `Face::Expression` index

### `kBaseTargets` for verbs and overlays

**Still used at runtime**, but **not exposed in the editor UI** (except via verb
timelines for verbs).

Firmware comment ([`FACE_CONFIG_DATA.h`](../robot_v3/src/face/FACE_CONFIG_DATA.h)):

> Verb / overlay rows are zero-filled; geometry and arm come from `kVerbTimelines`.

Runtime behaviour (firmware [`FrameEffective.cpp`](../robot_v3/src/face/FrameEffective.cpp),
editor [`frameController.ts`](src/app/_lib/face-engine/frameController.ts)):

1. Resolve expression → `baseTargets[expression]` (or blended emotion base during verb preview).
2. **`tickEffectiveParams`**: smooth toward base, sample verb timeline, **`combineEmotionVerbFace`** → one `FaceParams` row (28 fields, including arm).
3. **`MotionBehaviors::tick`**: drive servo from effective `arm_*` fields.
4. **`FrameController`**: body bob maps **live arm angle** (`Motion::currentOffsetDeg()`) across min→max deg to ±`bob_amplitude_px` (from idle row).

Loop order on device: `SceneContextFill` → `tickEffectiveParams` → `MotionBehaviors` → `Motion::tick` → `Face::tick`.

**Overlays** (`OverlayWaking`, `OverlayAttention`) have no verb timelines; their
`kBaseTargets` rows matter more and are also **not** editable in UI.

### Idle / blink / gaze / bob — `kIdleAnim`

Per-expression blink intervals, gaze style (`Off`, `IdleRandom`, `Orbit`, `ScanX`),
scan/amplitude fields, `bob_amplitude_px` (or `BOB_AMP_FOLLOW_EMOTION_ARM` to use
the effective arm sweep span as bob amplitude).

Body bob **position** follows commanded arm angle, not a separate phase clock tied
to `arm_period_ms`. See [`docs/firmware2/MOTION.md`](../docs/firmware2/MOTION.md).

**Planned:** dedicated `kIdleAnim` editor UI.

### Simulation tunables

| Table                  | Purpose                                                                               |
| ---------------------- | ------------------------------------------------------------------------------------- |
| `kEmotionSim`          | Emotion activation / valence smoothing, snap hysteresis                               |
| `kFrameAnim`           | Tick rate, geometry smooth tau, breath, thinking flip timing, mood ring tau, defaults |
| `kVerbSim`             | Strain delay, default overlay duration                                                |
| `kVerbTransitionDurMs` | Cross-fade when switching verb timelines                                              |

**Planned:** single “simulation / tuning” panel in the editor.

### Derived data (not hand-edited)

-   `emotionTriangulation.triangles` — always Delaunay from `kEmotionPoints` / anchors
-   `emotionTriangulation.domain` — fixed `[-1,1] × [0,1]`

---

## 4. Planned editor directions (from product notes)

1. **Schema updates** — custom emotion names/counts; pick order and expression mapping UI.
2. **Verbs** — timelines remain primary; optional UI for verb `kBaseTargets` base rows.
3. **Idle** — `kIdleAnim` editor (blink, gaze, bob amplitude policy).
4. **Sim panel** — `kEmotionSim` / `kFrameAnim` / `kVerbSim` / transition ms in one place.

Arm tuning is **in scope today** via emotion inspector and verb keyframes — no
separate `kArmPresets` / `kMotion` tables.

---

## 5. Quick reference — UI modes

```
┌─────────────────────────────────────────────────────────────┐
│  Save Face Config Data                                       │
├─────────────────────────────────────────────────────────────┤
│  [ Emotions ]  [ Verbs ]     ← PanelModeSwitcher              │
├─────────────────────────────────────────────────────────────┤
│  EMOTIONS MODE                                               │
│    BlendPanel: V/A sliders, blend diagram (anchor drag)        │
│    EmotionButtons: bridge only                               │
│    → EmotionPointInspector: kBaseTargets[emotion] (28 fields) │
│                                                              │
│  VERBS MODE                                                  │
│    VerbTimelinePanel + KeyframeInspector: kVerbTimelines     │
│    (arm_* overrides optional per keyframe)                    │
│    (optional StaticModePanel when inspector closed)           │
└─────────────────────────────────────────────────────────────┘
```

---

## 6. Files touched by Save

| Output                 | Path                                                                  |
| ---------------------- | --------------------------------------------------------------------- |
| TypeScript data        | `face-editor/src/app/_lib/face-engine/FACE_CONFIG_DATA.ts`            |
| Firmware data          | `robot_v3/src/face/FACE_CONFIG_DATA.h`                                |
| Firmware triangulation | `robot_v3/src/behaviour/EmotionTriangulation.h`                       |
| Reload snapshot        | `face-editor/src/app/_lib/face-engine/FACE_CONFIG_DATA.snapshot.json` |

Codegen: [`src/app/_lib/face-config-codegen/`](src/app/_lib/face-config-codegen/) (Node,
`delaunator`, no Python). Snapshot v2 configs migrate to v3 on load
([`migrateSchemaV3.ts`](src/app/_lib/face-config-codegen/migrateSchemaV3.ts)).
