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

---

## 1. High-level map of `FaceConfigState`

| Section                 | Role                                                                                      |
| ----------------------- | ----------------------------------------------------------------------------------------- |
| **Expression schema**   | Fixed list of 22 `Face::Expression` values, which are “emotions” vs verbs/overlays        |
| **Named emotions (14)** | V/A anchors, pick order, mapping to expression indices                                    |
| **Triangulation**       | Delaunay mesh over emotion anchors (computed at load/save, not stored as source of truth) |
| **`kBaseTargets`**      | 22 × 24 `ParamI16` face geometry rows (value + strength per field)                        |
| **`kVerbTimelines`**    | Per-verb keyframed override tracks (time → partial field overrides)                       |
| **`kArmPresets`**       | Per-expression arm waggle ranges (for blended emotion arm motion)                         |
| **`kMotion`**           | Per-expression body/arm motion mode (oscillate, waggle, thinking, etc.)                   |
| **`kIdleAnim`**         | Per-expression blink, gaze, and face-bob policy                                           |
| **Sim tunables**        | `kEmotionSim`, `kFrameAnim`, `kVerbSim`, `kMotionRuntime`, `kVerbTransitionDurMs`         |

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

### Emotion face geometry — `kBaseTargets` (14 emotion rows only)

| What                                                     | UI location                                                                                                                                                      |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-field **value** and **strength** for a named emotion | **Emotions** mode → click anchor on blend diagram → [`EmotionPointInspector.tsx`](src/app/_components/face-editor/EmotionPointInspector.tsx) (`ParamSliderGrid`) |
| Valence / arousal readout                                | Top of `EmotionPointInspector` (from live anchor)                                                                                                                |

Rows addressed by **PascalCase** emotion name (`Neutral`, `Happy`, …) matching
triangulation anchors — not by expression index dropdown.

**Not in UI:** direct editor for verb/overlay rows in `kBaseTargets` (see §3).

---

### Verb face animation — `kVerbTimelines`

| What                                             | UI location                                                                                                                                                                          |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Keyframe times, override fields/values/strengths | **Verbs** mode → [`VerbTimelinePanel.tsx`](src/app/_components/face-editor/VerbTimelinePanel.tsx) + [`KeyframeInspector.tsx`](src/app/_components/face-editor/KeyframeInspector.tsx) |
| Loop duration                                    | Verb timeline panel                                                                                                                                                                  |
| Preview playhead / speed                         | Verb timeline panel + [`FaceStage`](src/app/_components/face-editor/FaceStage.tsx) canvas                                                                                            |

Applies to expressions that use verb timelines: `VerbThinking`, `VerbReading`,
`VerbWriting`, `VerbExecuting`, `VerbStraining`, `VerbSleeping`.

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
-   `bobAmpFollowEmotionArm` (sentinel `0x8000` for “follow arm” bob)

### Emotion metadata

-   `pickOrderIndices` — tie-break when two anchors are equidistant
-   `namedEmotionToExpressionIndex` — maps each named emotion → `Face::Expression` index

### `kBaseTargets` for verbs and overlays

**Still used at runtime**, but **not exposed in the editor UI**.

Firmware comment ([`FACE_CONFIG_DATA.h`](../robot_v3/src/face/FACE_CONFIG_DATA.h)):

> Verb / overlay rows duplicate the same tuned geometry as `kVerbTimelines` keyframe 0.

Runtime behaviour ([`frameController.ts`](src/app/_lib/face-engine/frameController.ts)):

1. Start from `baseTargets[expression]` for the active expression.
2. Optionally replace with **blended emotion** geometry when in verb-timeline preview with a V/A base.
3. Smooth toward that row.
4. Layer **`kVerbTimelines`** via `sampleEffectiveVerb` + `combineEmotionVerbFace`.

So for active verbs, the **timeline is the practical source of truth** for the
animated face; verb `kBaseTargets` rows are still the default base layer (and match
keyframe 0 in practice). The editor does not provide a “edit VerbThinking base
row” screen — only keyframes.

**Overlays** (`OverlayWaking`, `OverlayAttention`) have timeline-less behaviour;
their `kBaseTargets` rows matter more and are also **not** editable in UI.

### Arm motion — `kArmPresets`

Per-expression arm waggle parameters used when blending emotion arm motion
([`emotionBlend.ts`](src/app/_lib/face-engine/emotionBlend.ts)).

**Planned:** dedicated arm-motion editor UI.

### Body motion — `kMotion`

Per-expression motion mode (`RandomDrift`, `Oscillate`, `Waggle`, `Thinking`, …),
period, amplitude, slew.

Drives arm period / body behaviour in [`frameController.ts`](src/app/_lib/face-engine/frameController.ts).

**Planned:** editor UI (likely alongside arm presets).

### Idle / blink / gaze / bob — `kIdleAnim`

Per-expression blink intervals, gaze style (`Off`, `IdleRandom`, `Orbit`, `ScanX`),
scan/amplitude fields, `bob_amplitude_px` (or `BOB_AMP_FOLLOW_EMOTION_ARM`).

**Planned:** editor UI.

### Simulation tunables

| Table                  | Purpose                                                                               |
| ---------------------- | ------------------------------------------------------------------------------------- |
| `kEmotionSim`          | Emotion activation / valence smoothing, snap hysteresis                               |
| `kFrameAnim`           | Tick rate, geometry smooth tau, breath, thinking flip timing, mood ring tau, defaults |
| `kVerbSim`             | Strain delay, default overlay duration                                                |
| `kMotionRuntime`       | Default slew times for motion modes                                                   |
| `kVerbTransitionDurMs` | Cross-fade when switching verb timelines                                              |

**Planned:** single large “simulation / tuning” panel in the editor.

### Derived data (not hand-edited)

-   `emotionTriangulation.triangles` — always Delaunay from `kEmotionPoints` / anchors
-   `emotionTriangulation.domain` — fixed `[-1,1] × [0,1]`

---

## 4. Planned editor directions (from product notes)

1. **Schema updates** — custom emotion names/counts; pick order and expression mapping UI.
2. **Verbs** — treat timelines as primary; consider whether verb `kBaseTargets` rows stay as duplicates of keyframe 0 or are generated-only.
3. **Arm / motion** — `kArmPresets` + `kMotion` editors.
4. **Idle** — `kIdleAnim` editor (blink, gaze, bob).
5. **Sim panel** — all `kEmotionSim` / `kFrameAnim` / `kVerbSim` / `kMotionRuntime` / transition ms in one place.

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
│    → EmotionPointInspector: kBaseTargets[emotion]            │
│                                                              │
│  VERBS MODE                                                  │
│    VerbTimelinePanel + KeyframeInspector: kVerbTimelines     │
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
`delaunator`, no Python).
