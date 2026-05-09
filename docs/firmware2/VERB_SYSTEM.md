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
- `src/face/VerbTimeline.{h,cpp}` — sample sparse field overrides for
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

## Verb timeline (face geometry overrides)

Each verb can override face geometry on top of whatever the emotion
blend produced. The format today is **sparse override** — a per-field
list of `{ FieldIndex, value, strength }` triples. The verb's
overrides for the 28 `FaceParams` fields are read out into two parallel
arrays (`hasField[28]`, `fieldVals[28]`), and `FrameController` then
calls `combineEmotionVerbFace()` to merge them with the smoothed
emotion preset (see [`FRAME_CONTROLLER.md`](FRAME_CONTROLLER.md), step
7).

```cpp
struct SparseOverride {
    FieldIndex field;
    int16_t    value;
    uint8_t    strength;   // 0..100
};

struct SparseVerbTimeline {
    Expression verb;
    uint8_t    count;
    SparseOverride entries[32];
};
```

Currently five verbs ship overrides: `VerbThinking`, `VerbReading`,
`VerbWriting`, `VerbExecuting`, `VerbStraining`. `VerbSleeping` has no
overrides yet (it falls through to the emotion blend for geometry).

The sampling function is:

```cpp
void sampleVerbTimeline(
    Expression verb,
    uint32_t   time_in_verb_ms,
    bool*      hasField,    // out: 28
    ParamI16*  fieldVals);  // out: 28
```

`time_in_verb_ms` is **currently ignored** — sparse overrides are
constant for the lifetime of the verb. The signature already accepts
time so future keyframed timelines can be dropped in without changing
any caller. When that lands, the planned shape is: each verb gets a
list of keyframes, each keyframe is `{ t_ms, SparseOverride[] }`, and
`sampleVerbTimeline` lerps neighbouring keyframes' values (and
strengths) at the requested time. Field strength stays the gate so
fields the verb *doesn't* address keep falling through to the emotion
blend.

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
`ring_r/g/b` values in the verb's sparse-override row, blended with
the emotion ring via the same combine as any other field.
