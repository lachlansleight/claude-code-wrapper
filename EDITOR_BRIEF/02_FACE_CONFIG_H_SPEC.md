# `FACE_CONFIG.h` — Specification

**Path:** `robot_v3/src/face/FACE_CONFIG.h`  
**Producer:** face editor “Export” (Phase 2) or hand-written only during
bring-up.  
**Consumer:** firmware `#include` + `scripts/gen_emotion_triangulation.py`
(regex parse).

**Normative pipeline:** [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md).  
**Locked forks (PR strategy, tween scope, combine family, idle layer):**
[`08_DECISIONS.md`](08_DECISIONS.md).

Banner on every generated file:

```cpp
// !!! GENERATED FILE - DO NOT EDIT !!!
// face-editor v<version>  <ISO-8601>
// Re-run: python scripts/gen_emotion_triangulation.py
```

## Design goals

1. **Single source of truth** for everything the editor authors (geometry
   presets, strengths, anchors, verb timelines, **`kMotion`**, idle policy).
2. **Compile-time only** — `constexpr` tables, no JSON in flash.
3. **Stable enum order** — `Expression::N` must match `kExpressions[N]`;
   same for `NamedEmotion` / `kEmotions[]`.
4. **Diff-friendly** export — deterministic formatting (float width, sign
   on valence, no churn).

## Namespace and enums

```cpp
namespace FaceConfig {

enum class Expression : uint8_t { /* … */, Count };
enum class NamedEmotion : uint8_t { /* … */, Count };

enum class ExpressionKind : uint8_t {
  Emotion,   // V/A blend + appears in kEmotions[]
  Verb,      // Face geometry from VerbTimeline only (no static preset row)
  Overlay,   // Routing / legacy enum slots only — bespoke pixels from EffectsRenderer
};
```

**`ExpressionKind::Verb`:** `kVerbTimelines[]` holds the face. The
`kExpressions[]` row for a verb still carries **id**, **name**, flags, and
may carry **unused** `FaceParams` placeholders for struct layout — firmware
**must not** read verb `params` for rendering after sub-phase B (`08`).

**`ExpressionKind::Overlay`:** `VerbSystem` may still map waking / attention
to an `Expression` for **routing**. **Parametric** face rows for overlays
are **deprecated** in favour of **`EffectsRenderer`**; do not send users to
the editor to “draw OverlayWaking”.

## `ParamI16` and `FaceParams`

Every geometric channel is a **`(value, strength)`** pair (`0…100`).
Semantics: [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)
§ Change 1. **`W == 0`** handling: [`08_DECISIONS.md`](08_DECISIONS.md).

```cpp
struct ParamI16 {
  int16_t value;
  uint8_t strength;  // 0 = abstain, 100 = full insistence
};

struct FaceParams {
  ParamI16 eye_dy, eye_rx;
  ParamI16 eye_top_apex, eye_top_corner, eye_bot_apex, eye_bot_corner, eye_thick;
  ParamI16 eye_wave_amp, eye_wave_freq, eye_wave_speed;
  ParamI16 pupil_dx, pupil_dy, pupil_r;
  ParamI16 mouth_dy, mouth_rx;
  ParamI16 mouth_top_apex, mouth_top_corner, mouth_bot_apex, mouth_bot_corner, mouth_thick;
  ParamI16 mouth_wave_amp, mouth_wave_freq, mouth_wave_speed;
  ParamI16 face_rot, face_y;
  ParamI16 ring_r, ring_g, ring_b;
};
```

## `FieldIndex` (verb keyframes + idle channels)

Canonical **0-based** indices for **`VerbKeyframe` overrides** match the
declaration order of **`FaceParams`** geometry fields first (`EyeDy` …
`RingB`).

**Extension:** idle / presentation scalars (blink interval min/max, blink
duration, bob Hz, bob amount, bob offset, gaze randomness, gaze reroll
min/max, gaze move duration, …) append **after** the geometry block so
**verb timelines can override idle policy** the same way as eyelids. The
exact count and names are fixed when PR-C lands; firmware and editor share
the generated `FieldIndex` enum.

## `ExpressionRow`

```cpp
struct ExpressionRow {
  Expression       id;
  const char*      name;              // snake_case
  ExpressionKind   kind;
  bool             mood_ring_enabled; // replaces moodRingEnabledFor switch
  FaceParams       params;            // Emotion: full. Verb: unused @ runtime. Overlay: deprecated
};
```

## Verb timelines

```cpp
static constexpr uint8_t kMaxOverridesPerKeyframe = 32;
static constexpr uint8_t kMaxKeyframesPerVerb     = 16;  // tune vs flash

struct KeyframeOverride {
  FieldIndex fieldIndex;
  int16_t    targetValue;
  uint8_t    strength;
};

struct VerbKeyframe {
  uint32_t   time_ms;
  uint8_t    override_count;
  KeyframeOverride overrides[kMaxOverridesPerKeyframe];
};

struct VerbTimeline {
  Expression   verb;
  uint32_t     loop_duration_ms;
  uint8_t      keyframe_count;
  VerbKeyframe keyframes[kMaxKeyframesPerVerb];
};
```

Sampling and wrap rules: **`06`** § Change 2. If a **`fieldIndex` never
appears** in the timeline, the verb **does not** touch that channel.

## `kMotion[]` (per expression)

Move **`MotionBehaviors::` period table** here — one row per
`Expression`, same index contract. Exact struct matches today’s use
(period ms + any fields `kMotion[]` currently carries). See PR-C in
[`03_FIRMWARE_REFACTOR.md`](03_FIRMWARE_REFACTOR.md).

## Idle animation policy (per expression)

Authorable **`IdleAnimParams`** (name TBD) **per `Expression`**, containing
at least (`08`):

| Group | Fields |
|-------|--------|
| Blink | interval **min/max** (ms); **duration** (ms) |
| Bob | **frequency** (Hz); **amount** (px); **offset** (px) |
| Gaze | **random** ∈ [0,1]; reroll **interval min/max** (ms); **move duration** (ms) |

Each numeric channel is stored as **`ParamI16`** if it participates in the
emotion+verb **resolution** pipeline; **apply** blink/gaze/bob to drawn
**pixels only after** the verb combine (`08`).

## `EmotionRow`

```cpp
struct EmotionArmMotion { /* unchanged */ };

struct EmotionRow {
  NamedEmotion     id;
  Expression       expression;
  float            valence;
  float            activation;
  uint8_t          tie_break_rank;  // permutation of 0..Count-1
  EmotionArmMotion arm;
};
```

## Arrays and accessors

- `static constexpr ExpressionRow kExpressions[]`
- `static constexpr EmotionRow kEmotions[]`
- `static constexpr VerbTimeline kVerbTimelines[]`
- `static constexpr MotionRow kMotion[]` (name TBD)
- `static constexpr IdleAnimRow kIdleAnim[]` (name TBD)

Provide **`constexpr`** accessors: `paramsFor` (emotions only or guarded),
`isEmotionExpression`, `expressionName`, `emotionName`, `motionPeriodMsFor`,
`idleParamsFor`, etc.

## Export validation (editor)

1. Enum order matches row order.  
2. `tie_break_rank` is a **permutation**.  
3. Each `kEmotions[i].expression` points at **`kind == Emotion`**.  
4. Duplicate `(v, a)` — warn; **`tie_break_rank`** must break ties.  
5. Stable float / int formatting for git diffs.

## Not in this file

- **`EmotionTriangulation.h`** — still **Python-generated**.  
- **VerbSystem policy** (linger, queueing) — firmware.  
- **`NamedColor` / `g_defaultColors` / `accentNamedColor()`** — firmware
  (`08`).  
- **`EffectsRenderer`** assets / curves — firmware.

## Resolved (formerly “open questions”)

| Topic | Resolution |
|-------|------------|
| Enums in header vs old headers | **`08`:** enums live in **`FACE_CONFIG.h`**, re-export if needed. |
| `kMotion[]` location | **`FACE_CONFIG.h`** |
| Blink/gaze/bob | **`FACE_CONFIG.h`**, post-verb application (`08`) |
| `NamedColor` in header | **No** (Phase 1) |
