# Current State — Where Face Data Lives Today

> **Legacy snapshot.** This file describes **today’s** firmware layout and
> inlines tables for **bootstrap migration** into `FACE_CONFIG.h`. The
> **target** runtime is defined in [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)
> and [`08_DECISIONS.md`](08_DECISIONS.md) (verbs become timelines; verb
> static rows go away after sub-phase B).

This is the inventory of every place the firmware currently encodes
expression / emotion / motion polish data. Phase 1 (see
[`03_FIRMWARE_REFACTOR.md`](03_FIRMWARE_REFACTOR.md)) collapses **authored**
tables into **`FACE_CONFIG.h`**. The editor’s data model must be able to
round-trip that header.

The original wiring guide in the firmware repo is
`docs/firmware/KBASETARGETS_WIRING.md` — it lists 12+ files that have
to change in lock-step every time an emotion is added. That's the
problem we are solving.

## The two big tables the migration bootstraps from

After the pipeline overhaul, the **editor** produces a superset (emotion
`ParamI16` rows, verb timelines, motion, idle policy). For **sub-phase A**
(consolidate only), mechanically lift the literals below into
`FACE_CONFIG.h` without changing numbers — see [`03_FIRMWARE_REFACTOR.md`](03_FIRMWARE_REFACTOR.md).

### 1. `kBaseTargets[]` — one `FaceParams` per `Expression`

Lives in `robot_v3/src/face/FrameController.cpp`. Indexed by the
`Face::Expression` enum (declared in `SceneTypes.h`); row order **is
the contract**.

```cpp
// Field order:
//   eye_dy, eye_rx,
//   eye_top_apex, eye_top_corner, eye_bot_apex, eye_bot_corner, eye_thick,
//   eye_wave_amp, eye_wave_freq, eye_wave_speed,
//   pupil_dx, pupil_dy, pupil_r,
//   mouth_dy, mouth_rx,
//   mouth_top_apex, mouth_top_corner, mouth_bot_apex, mouth_bot_corner, mouth_thick,
//   mouth_wave_amp, mouth_wave_freq, mouth_wave_speed,
//   face_rot, face_y,
//   ring_r, ring_g, ring_b
static const FaceParams kBaseTargets[(uint8_t)Expression::Count] = {
    /* Neutral */          {  2, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  3, 15,
                              0, 15,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 },
    /* Happy */            {  0, 30,  -16, 0, +30, 0, 3,  0, 0, 0,   0,  5,  16,
                              0,  +24,   3, 0,  3, 0, 3,  0, 0, 0,
                              0, 5,    0, 0, 0 },
    /* Excited */          {  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   0,  0,  17,
                              0,  +27,   4, -2,  8, -2, 3,  0, 0, 0,
                              0, 0,    40, 255, 80 },
    /* Joyful */           { -5, 20,  -15, 0, -6, 0, 4,  0, 0, 0,   0,  0,  14,
                              -11,  +37,   3, 0,  24, 0, 4,  0, 0, 0,
                              0, -14,    255, 228, 38 },
    /* Sad */              {  4, 28,  -12, 0, +17, 0, 3,  0, 0, 0,   0,  3,  11,
                              4,  +20,   -13, -7,  -11, -8, 3,  0, 0, 0,
                              0, 6,    0, 0, 0 },
    /* VerbThinking */     {  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   7, -9, 15,
                              0, 11,   +3, 0,  +3, 0, 3,  0, 0, 0,
                            -10, 0,    36, 56, 120 },
    /* VerbReading */      {  0, 28,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  8, 12,
                              0,  9,   +3, 0,  +3, 0, 3,  0, 0, 0,
                              0, 12,   78, 146, 210 },
    /* VerbWriting */      {  0, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0, -8, 15,
                              0, 15,    0, 0, +14, 0, 3,  0, 0, 0,
                              0, 0,    104, 118, 228 },
    /* VerbExecuting */    {  0, 30,  -16, 0, +16, 0, 3,  0, 0, 0,   0, -4, 10,
                              0,  9,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    156, 64, 216 },
    /* VerbStraining */    {  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
                              0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
                              0, 0,    210, 75, 220 },
    /* VerbSleeping */     {  8, 26,   -2, 0,  +2, 0, 3,  0, 0, 0,   0,  0,  15,
                              0,  9,    0, 0,   0, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 },
    /* OverlayWaking */    { -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                              0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                              0, 0,    128, 128, 128 },
    /* OverlayAttention */ { -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                              0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                              0, 0,    255, 20, 40 },
    /* Sleepy */           {  0, 28,  0, 10, +34, 10, 3,  0, 0, 0,   0,  0,  15,
                              0,  +13,   0, 0,  3, 0, 3,  0, 17, 90,
                              0, 9,    0, 0, 0 },
    /* Distressed */       {  2, 30,  -26, 0, +33, 0, 3,  0, 0, 0,   0,  7,  10,
                              4,  +24,   -19, -7,  -7, 0, 3,  0, 0, 0,
                              0, -15,    255, 48, 24 },
    /* Blissed */          {  1, 20,   +3, 0, +1, 0, 3,  0, 0, 0,   0,  0,  15,
                              1,  +26,   3, 0,  13, 0, 3,  0, 0, 0,
                              0, 5,    0, 0, 0 },
    /* Depressed */        {  0, 30,   +16, 10, +34, 11, 3,  0, 0, 0,   0,  20,  6,
                              0,  +13,   0, +6,  3, 4, 3,  0, 17, 90,
                              0, 9,    0, 0, 0 },
    /* Shocked */          {  0, 30,   -34, 0,   39, 0, 3,   1, 85, 720,   0, 3, 9,
                             20, 17,  -17, 0,   8, 0, 1,    2, 49, 720,
                              0, 0,     255, 255, 255 },
    /* Disappointed */     {  3, 21,   +6, 0, +6, 0, 3,  0, 0, 0,   0,  3,  8,
                              5,  +26,   -1, 0,  -3, 0, 3,  0, 0, 0,
                              0, 0,    229, 54, 95 },
    /* Cheeky */           {  1, 30,  -31, 0, +8, 0, 3,  0, 0, 0,   0,  3,  15,
                              -25,  +15,   11, 0,  8, 0, 3,  0, 0, 0,
                              0, -3,    0, 0, 0 },
    /* Gleeful */          {  1, 27,  -30, 0, -2, 0, 3,  0, 0, 0,   0,  -7,  10,
                              -25,  +27,   0, -2,  20, -2, 3,  0, 0, 0,
                              0, 5,    39, 248, 78 },
    /* Frustrated */       {  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
                              0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
                              0, 0,    210, 75, 220 },
};
```

### 2. `kEmotionPoints[]` — one `(v, a)` per emotion

Lives in `robot_v3/src/behaviour/EmotionSystem.cpp`. Indexed by the
`EmotionSystem::NamedEmotion` enum. Same row order contract.

```cpp
constexpr EmotionPoint kEmotionPoints[(size_t)NamedEmotion::Count] = {
    {+0.0f, 0.5f},    // Neutral
    {+0.5f, 0.5f},    // Happy
    {+1.0f, 0.6f},    // Excited
    {+1.0f, 1.0f},    // Joyful
    {-0.5f, 0.5f},    // Sad
    {-0.2f, 0.0f},    // Sleepy
    {-1.0f, 1.0f},    // Distressed
    {+1.0f, 0.0f},    // Blissed
    {-1.0f, 0.0f},    // Depressed
    {-0.3f, 1.0f},    // Shocked
    {-1.0f, 0.3f},    // Disappointed
    {+0.5f, 0.7f},    // Cheeky
    {+0.6f, 1.0f},    // Gleeful
    {-0.6f, 0.8f},    // Frustrated
};
```

`v` ∈ [-1, +1], `a` ∈ [0, 1]. Only **emotion** expressions (not verbs
or overlays) appear here.

### 2b. `kPickOrder[]` — tie-break order for nearest-anchor snap

When two anchors are equidistant from `(v, a)`, the one listed
earlier in `kPickOrder[]` wins. Must list every `NamedEmotion`
exactly once. Same file as `kEmotionPoints`.

```cpp
static constexpr NamedEmotion kPickOrder[] = {
    NamedEmotion::Gleeful,
    NamedEmotion::Cheeky,
    NamedEmotion::Sleepy,
    NamedEmotion::Distressed,
    NamedEmotion::Frustrated,
    NamedEmotion::Disappointed,
    NamedEmotion::Blissed,
    NamedEmotion::Depressed,
    NamedEmotion::Shocked,
    NamedEmotion::Neutral,
    NamedEmotion::Happy,
    NamedEmotion::Excited,
    NamedEmotion::Joyful,
    NamedEmotion::Sad,
};
```

## Other tables — where they move (Phase 1 target)

| Source | Today's location | After Phase 1 (`08` locked) |
|---|---|---|
| `Expression` / `NamedEmotion` | `SceneTypes.h`, `EmotionSystem.h` | Declared in or re-exported from **`FACE_CONFIG.h`** (single source). |
| `isEmotionExpression()` | `SceneTypes.h` | **`FaceConfig::`** (from `ExpressionKind`). |
| `expressionName()` / `emotionName()` | `SceneTypes.cpp`, `EmotionSystem.cpp` | **`FaceConfig::`** string pointers in tables. |
| `expressionForNamedEmotion()` etc. | `EmotionBlend.cpp`, `SceneContextFill.cpp` | **`kEmotions[].expression`** + accessors. |
| `armPresetFor()` | `EmotionBlend.cpp` | **`kEmotions[].arm`** |
| `accentNamedColor()` | `SceneContextFill.cpp` | **Stays firmware** (debug/TextScene only today). |
| `kMotion[]` | `MotionBehaviors.cpp` | **`FACE_CONFIG.h`** |
| `blinkPeriodMsFor()`, `gazeFor()`, `bodyBobFor()` | `FrameController.cpp` | Replaced by **authorable idle policy** in **`FACE_CONFIG.h`** + subsystem (`08`). |
| `moodRingEnabledFor()` | `MoodRingRenderer.cpp` | **`kExpressions[].mood_ring_enabled`** |
| `g_defaultColors[]`, `NamedColor`, `tryParseNamedColor()` | Settings / BridgeControl | **Stay firmware** (Phase 1). |
| Triangulation header / JS | generated | Still **generated**; input parsed from **`FACE_CONFIG.h`**. |
| `control/simulator_v3.html` + JS | repo | **Keep** as reference; **no** sync requirement until editor ships (`08`). |

## The triangulation generator

`scripts/gen_emotion_triangulation.py` **today** reads anchor coordinates
from `EmotionSystem.cpp`. After Phase 1 it reads the same floats from
**`robot_v3/src/face/FACE_CONFIG.h`** (`kEmotions[]` or equivalent). The
**Bowyer–Watson** implementation and output paths are unchanged:

- `robot_v3/src/behaviour/EmotionTriangulation.h`
- `control/scripts/emotion-triangulation.js` (legacy simulator only)

The triangulation **output** remains **generated, not hand-edited**. The
editor (or export script) must run Python after every `FACE_CONFIG.h` write.
