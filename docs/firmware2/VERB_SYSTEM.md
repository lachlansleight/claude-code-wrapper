# Verb system and verb timelines

While the emotion system answers "how does the robot feel?", the **verb
system** answers "what is the robot doing?". Verbs are discrete, named
states like `Reading` or `Executing`. They are layered with emotions in
two ways:

- The **expression** displayed on the face is verb-derived if a verb is
  active, otherwise emotion-derived (`SceneContextFill`).
- The **arm motion** is driven by the verb's `kMotion` row when a verb
  is active, otherwise by the blended emotion arm preset.

Source files:

- `src/behaviour/VerbSystem.{h,cpp}` — the state machine.
- `src/face/VerbTimeline.{h,cpp}` — sample keyframed verb timelines for
  the active verb.
- Verb-related data lives in `src/face/FACE_CONFIG_DATA.h`.

## Verb enum

```cpp
enum class Verb : uint8_t {
    None,
    Sleeping,
    Thinking,
    Reading,
    Writing,
    Executing,
    Straining,
    Waking,                 // overlay-only
    AttractingAttention,    // overlay-only
};
```

`None` means "no verb in flight" (the face will fall back to the
emotion-derived expression). `Sleeping` is the boot/long-idle state.
`Thinking` is the default "alive but idle" state once a session has
started. `Waking` and `AttractingAttention` are **overlay-only** verbs:
calling `setVerb(Waking)` does not change the base verb — it fires a
short transient overlay (typically 1 s) on top of whatever the base
verb is, and afterwards execution returns to the base.

Each verb maps 1:1 to an `Expression` of the same name (`VerbThinking`,
`VerbReading`, …) which is what the face system actually consumes. The
`Verb` enum is the behavioural layer; `Expression` is the rendering
layer.

## Three-layer state

```
base       ── current long-running verb (Sleeping / Thinking / …)
linger     ── optional hold-window after armLinger() before decay
overlay    ── short transient verb (Waking, AttractingAttention)
```

`current()` returns the base verb (ignoring overlays). `effective()`
returns the overlay if one is active, else the base. The `EventRouter`
sets verbs; everyone downstream reads `effective()` via
`SceneContextFill`.

### Linger

When an activity finishes, the router calls `armLinger(ms)` (typically
1 s). The verb stays at its current value until that window elapses,
then auto-decays to `Thinking`. This prevents the verb from strobing
between `Reading`, `Writing`, and `Thinking` during a burst of
back-to-back tools. Any new `setVerb(...)` call clears the linger.

### Overlay queue

`fireOverlay(verb, durationMs)` plays an overlay. If another overlay is
already active, the new one is queued and fires when the active one
expires. Only one queued overlay is held; a third in-flight one is
dropped. There is also a `fireOverlay(verb, durationMs, postOverlayVerb)`
form that lets the caller specify what the base verb should become
when the overlay finishes — used by `session.started` to fire `Waking`
and then snap to `None` so the emotion system takes over again.

### Executing → Straining auto-promotion

If the base verb has been `Executing` for ≥ `kVerbSim.strain_delay_ms`
(default 5 s), `tick()` auto-promotes it to `Straining`. The intent is
that long-running shells start to look stressed. Combined with
`EventRouter`'s held driver, after another 30 s in `Straining` the
emotion goal is also pulled toward unhappy.

## Public API

```cpp
namespace VerbSystem {
    void begin();
    void tick();

    void setVerb(Verb v);              // overlay verbs route to fireOverlay
    void clearVerb();                  // → None
    void armLinger(uint32_t ms);

    void fireOverlay(Verb v, uint32_t durationMs);
    void fireOverlay(Verb v, uint32_t durationMs, Verb postOverlayVerb);

    Verb     current();                // base verb
    Verb     effective();              // overlay if active, else base
    bool     overlayActive();
    uint32_t enteredAtMs();
    uint32_t timeInCurrentMs();
    DebugState debugState();
}
```

## How EventRouter drives the verb system

| Bridge event                   | Action                                                                |
|--------------------------------|-----------------------------------------------------------------------|
| `session.started`              | Fire `Waking` overlay (1 s, post→`None`); impulse `+0.6 V, +0.6 A`    |
| `session.ended`                | `setVerb(Sleeping)`                                                   |
| `turn.started`                 | If `Sleeping`, fire `Waking` first then `Thinking`. Else `Thinking`.  |
| `turn.ended`                   | `clearVerb()`; impulse `+0.7 V, +0.9 A`                               |
| `activity.started` (file.read / search.* / web.* / mcp.* / unknown) | `setVerb(Reading)` |
| `activity.started` (file.write / file.delete / notebook.edit)        | `setVerb(Writing)` |
| `activity.started` (shell.exec / shell.background)                   | `setVerb(Executing)` |
| `activity.finished` / `failed` | `armLinger(1000)`                                                     |
| `notification` ("Claude needs ...") | Fire `AttractingAttention` overlay (1 s)                         |
| `permission.requested`         | Hold emotion driver `PendingPermission` at `v = -0.6`                 |
| `permission.resolved`          | Release `PendingPermission` driver                                    |

## Verb cross-fade

Verb changes do **not** snap on. The verb timeline sampler in
`face/VerbTimeline.{h,cpp}` cross-fades over **500 ms**
(`kVerbTransitionDurMs`) between the previous effective sample and the
new target's timeline sample. This applies to every transition:

- **None → verb** — strength of every overridden field ramps from 0 → 1.
- **verb → None** — strength of every overridden field ramps from 1 → 0.
- **verb A → verb B** — per-field rules (see below).
- **mid-transition retarget** — at the moment the verb changes, the
  current in-flight effective sample is **snapshotted** as the new
  `from` side, and the fade restarts toward the new `to`. So a burst of
  rapid retargets like `Read → Write → Read` glides smoothly without
  popping back through partial intermediate states.

### Per-field blend rules

For each of the 28 `FaceParams` fields, with `t ∈ [0, 1]` the
transition fraction:

- Both `from` and `to` override the field → lerp value AND strength by
  `t`.
- Only `from` overrides → keep value, scale strength by `(1 - t)`.
- Only `to` overrides → keep value, scale strength by `t`.
- A field whose blended strength rounds to 0 is dropped (`hasField`
  becomes false), so it falls through to the underlying emotion in the
  combine pass.

Because the [emotion+verb combine](FRAME_CONTROLLER.md) treats verb
strength as the lerp `t` against the emotion baseline, scaling strength
toward 0 *is* the ramp-out — no separate "verb intensity" knob is
needed.

### Modification-pass cross-fade

Several per-frame outputs in `FrameController` are keyed by the active
expression and would otherwise snap on a verb change:

- **Body bob amplitude** (`bodyBobAmpFor` is a pure function of the
  effective expression / context).
- **Gaze offset** — idle patterns (IdleRandom / Orbit / ScanX) only run
  when the effective expression is **not** on a verb timeline; during
  verbs the live gaze is `(0, 0)` so geometry comes from the timeline.

Body bob amplitude is cross-faded over the same 500 ms window using
`Face::verbTransitionT(now)`. Gaze uses the same fade: at the verb edge
the last rendered offset is snapshotted, then lerped toward the new
live value (verb → emotion can ramp out of a frozen offset smoothly).
The integrated body-bob phase keeps running so the oscillation is
continuous across the fade.

Two pieces are intentionally **not** cross-faded:

- **Servo arm motion** lives in `MotionBehaviors` / `Motion`, which
  already eases the physical servo via `playJog` and the emotion-arm
  sine integrator. Adding a face-side cross-fade would fight the
  servo's own slew. The simulator approximates the arm visually so it
  *does* cross-fade there for parity.
- **Blink close/open durations** read per-expression config but are
  only consulted at blink schedule time; an in-flight blink completes
  on its existing schedule, and the next is scheduled with the new
  expression's period. No mid-blink jump occurs.

### Public API

```cpp
namespace Face {
    constexpr uint32_t kVerbTransitionDurMs = 500;

    void sampleVerbTimeline(Expression verb, uint32_t time_in_verb_ms,
                            bool* hasField, ParamI16* fieldVals);

    void sampleEffectiveVerb(Expression currentVerbExpression, uint32_t nowMs,
                             uint32_t timeInVerbMs,
                             bool* hasField, ParamI16* fieldVals);

    void resetVerbTransition();
    float verbTransitionT(uint32_t nowMs);   // 0..1 over the active fade
}
```

`sampleEffectiveVerb` is the per-frame entry point used by
`FrameController`. It is the **only** path that should sample the verb
timeline once a frame loop is running — calling `sampleVerbTimeline`
directly bypasses the cross-fade. Pass any non-verb `Expression` (an
emotion, an overlay, or `Expression::Count`) as `currentVerbExpression`
to ramp the verb out toward an empty sample.

## Verb timeline (face geometry overrides)

Each verb can override face geometry on top of whatever the emotion
blend produced. All verb-specific face tuning lives in **`VerbTimeline`
rows** in `FACE_CONFIG_DATA.h`: a `loop_duration_ms`, a small list of
**keyframes**, and per-keyframe **sparse overrides**
(`KeyframeOverride`: field index, `targetValue`, `strength`). The
sampler expands overrides into `hasField[28]` / `fieldVals[28]`, and
`FrameController` calls `combineEmotionVerbFace()` to merge with the
smoothed emotion preset (see [`FRAME_CONTROLLER.md`](FRAME_CONTROLLER.md)).

```cpp
struct KeyframeOverride {
    uint8_t  field;         // Face::FieldIndex
    int16_t  targetValue;
    uint8_t  strength;      // 0..100
};

struct VerbKeyframe {
    uint32_t time_ms;
    uint8_t  override_count;
    KeyframeOverride overrides[kVerbKeyframeOverridesMax];
};

struct VerbTimeline {
    Face::Expression verb;
    uint32_t         loop_duration_ms;
    uint8_t          keyframe_count;
    VerbKeyframe     keyframes[kVerbKeyframesMax];
};
```

All six verb expressions have a row in `kVerbTimelines[]`. Shipped
data uses **one keyframe at `time_ms == 0`** (strength 100 on every
authored field) and a **1000 ms** loop placeholder; multi-keyframe
curves are supported by the sampler.

The sampling function is:

```cpp
void sampleVerbTimeline(
    Expression verb,
    uint32_t   time_in_verb_ms,
    bool*      hasField,    // out: 28
    ParamI16*  fieldVals);  // out: 28
```

`time_in_verb_ms` is taken **modulo** `loop_duration_ms` (non-zero).
With a single keyframe, the sample is the merged result of that
keyframe's overrides (see below). With multiple keyframes, the timeline
first builds a **cumulative state** after each keyframe in order, then
**lerps** between the cumulative state at the segment's left and right
keyframe indices (including the wrap segment from the last keyframe
back toward keyframe 0).

**Cumulative merge:** for keyframes `0 .. i` applied in order, each
override replaces that field's value; **`strength == 0` clears** the
field so the verb stops overriding it from that point onward (relinquish
to the emotion blend). A field set only in an early keyframe therefore
**holds** for the rest of the loop until a later keyframe touches it or
clears it with strength 0.

**Segment lerp:** for each `FaceParams` field, the sampler uses the same
rules as the verb cross-fade (`both` → lerp value and strength; `from`
only → decay strength toward 0 as `u → 1`; `to` only → ramp strength up
from 0). Fields absent from both cumulative endpoints stay off
(`hasField == false`).

## Verb → Expression → mood ring color

`SceneContextFill` maps each verb to its `Expression` and an accent
`NamedColor`:

| Verb              | Expression         | Accent NamedColor |
|-------------------|--------------------|-------------------|
| Sleeping          | VerbSleeping       | Sleeping          |
| Thinking          | VerbThinking       | Thinking          |
| Reading           | VerbReading        | Reading           |
| Writing           | VerbWriting        | Writing           |
| Executing         | VerbExecuting      | Executing         |
| Straining         | VerbStraining      | Straining         |
| Waking            | OverlayWaking      | (unused)          |
| AttractingAttention | OverlayAttention | Attention         |
| None              | (emotion-derived)  | (emotion-derived) |

The accent colour is the source of the activity-dot tint and the
accent palette entry. The mood ring colour comes from the
`ring_r/g/b` values in the verb's timeline overrides, blended with
the emotion ring via the same combine as any other field.
