# Emotion System (planned)

Design for the system that replaces the current monolithic `Personality`
module. Splits the single "what's the robot doing" answer into two
parallel concerns:

- **VerbSystem** — discrete *what the agent is currently doing*
  (thinking / reading / writing / executing / sleeping / etc.). One verb
  active at a time, or none.
- **EmotionSystem** — continuous *how the robot feels*, modelled as
  Valence (positive ↔ negative) and Activation (calm ↔ aroused).

The renderer composes them: **a continuous verb wins; otherwise the
snapped emotion drives the face.** One-shot animation overlays
(waking, attracting-attention) pre-empt both.

This doc is the implementation spec for the refactor planned in
[EXPRESSION_PLANS.md](EXPRESSION_PLANS.md). It's planned, not built —
some details (time constants, exact emotion coordinates) will need
tuning on hardware.

## Pipeline position

```
agent_event ──► EventRouter (the .ino's onAgentEvent)
                       │
        ┌──────────────┼──────────────┐
        ▼                              ▼
   VerbSystem                     EmotionSystem
   (verb,                         (V, A, persistent
    linger,                        V targets,
    one-shot                       exponential
    overlays)                      decay)
        │                              │
        └──────────────┬───────────────┘
                       ▼
               FrameController
       (verb-if-active else snap(V,A) → face,
        plus one-shot overlay if active)
```

Replaces the current single `Personality::current() → State` answer.
Both subsystems run in parallel each tick.

## EmotionSystem

### Model

Two floats, both in `[-1, +1]` (V) and `[0, +1]` (A):

```cpp
struct Emotion {
  float valence;      // -1 sad ─── 0 neutral ─── +1 joyful
  float activation;   //  0 calm ─── 1 aroused
};
```

### Triggers

Two ways an event affects emotion:

1. **Impulse** — instantaneous additive bump to A and/or V. Used for
   one-shot reactions like a turn ending in success. Clamped to range:
   `A = clamp(A + impulse_a, 0, 1)`, `V = clamp(V + impulse_v, -1, 1)`.
   Multiple impulses in quick succession stack (a celebration after a
   celebration *should* feel extra good).

2. **Persistent V target** — sets V's target to a held value while a
   condition lasts. V eases toward `target_v` (not toward 0) for as
   long as the driver holds it. When the driver releases, target
   returns to 0 and V eases back. If multiple drivers hold targets
   simultaneously, take the one with the largest magnitude (the "most
   significant" feeling wins).

   Critically: **the target is set to a value, it does not push at a
   rate.** A long-pending blocked permission settles V to (say) `-0.6`
   and *holds* there — it does not slowly drag V toward `-1`. Otherwise
   a minor annoyance over hours would max out negative.

### Decay

Both axes ease frame-by-frame. Frame-rate-independent form:

```cpp
float alpha = 1.0f - expf(-dt_ms / tau_ms);
A += (0.0f - A) * alpha_A;                          // A always toward 0
V += (current_target_v - V) * alpha_V;              // V toward held target (or 0)
```

Time constants (initial guesses — tune on hardware):

| Channel | τ        | What that feels like                                                            |
|---------|----------|---------------------------------------------------------------------------------|
| `A`     | ~6 s     | A turn-end celebration `A=1.0` decays to ~0.37 in 6 s, ~0.13 in 12 s.           |
| `V`     | ~90 s    | A win lifts V; mood lingers a minute or two before settling. Slow on purpose.   |

The current discrete decay chain (FINISHED 1.5s → EXCITED 10s → READY 60s
→ IDLE) becomes implicit: the snap result moves through "joyful → excited
→ happy → neutral" naturally as A decays.

### Snap-to-discrete emotion

For rendering, the continuous (V, A) point snaps to the nearest of a
small named-emotion set, by Euclidean distance in V/A space:

| Emotion   | V     | A    | Notes                                              |
|-----------|-------|------|----------------------------------------------------|
| Neutral   |  0.0  | 0.0  | the baseline; idle face                            |
| Happy     | +0.5  | 0.2  | "ready" — calm but pleased                         |
| Excited   | +0.6  | 0.6  | post-celebration energy                            |
| Joyful    | +0.9  | 0.9  | the celebration peak (was FINISHED)                |
| Sad       | -0.6  | 0.1  | the blocked face                                   |
| *Stressed*| -0.4  | 0.6  | (future) long-running blocked execution            |
| *Alert*   |  0.0  | 0.7  | (future) waiting tensely                           |

The starred ones aren't part of v1 — the current set is: Neutral,
Happy, Excited, Joyful, Sad. Add coordinates as new emotions are
introduced; the snap function doesn't change.

Snap with hysteresis (don't oscillate between two equidistant
emotions): re-snap only when the nearest emotion changes by margin
> ε for ≥ N ms. Concrete: `ε = 0.05`, hold for `100 ms`.

The snapped emotion is what `FrameController` reads; the rest of the
face (eased mood-ring colour, micro-modulators) can read raw V/A if
finer control is wanted later.

## VerbSystem

The verb is *what the agent is doing right now*. Discrete enum:

```cpp
enum Verb : uint8_t {
  NONE = 0,
  THINKING,
  READING,
  WRITING,
  EXECUTING,
  STRAINING,                  // executing >5 s — different look
  SLEEPING,
  // One-shot overlays (special handling, see below):
  WAKING,
  ATTRACTING_ATTENTION,
  kVerbCount,
};
```

### Continuous verbs

`THINKING` / `READING` / `WRITING` / `EXECUTING` / `STRAINING` /
`SLEEPING` persist until the system explicitly transitions them.
Triggers:

- `turn.started` → `THINKING`
- `activity.started` → verb based on `activity.kind` (`READING` /
  `WRITING` / `EXECUTING`)
- `activity.finished` → arm 1 s linger; if no other verb takes over,
  fall back to `THINKING`
- `EXECUTING` self-transitions to `STRAINING` after 5 s
- `turn.ended` → `NONE` (emotion takes over)
- `session.ended` → `SLEEPING`

`STRAINING` is "I've been at this a while". It's still a verb (still
showing the executing-style face), it just looks different (per-state
`FaceParams` row, like the current EXECUTING_LONG). After ~30 s of
`STRAINING`, the system *also* sets a persistent V target = `-0.4` so
the underlying mood reads stressed — but the verb itself stays as
straining until the activity ends. Once activity ends and V's target
releases, V eases back.

### One-shot overlay verbs

`WAKING` and `ATTRACTING_ATTENTION` are timed one-shot animations. They
**pre-empt** whatever was on screen, play their full animation, then
return to whatever verb (or none) was active when they fired.

```
sPreOverlayVerb saved on overlay entry
overlay plays for fixed duration
on overlay end: verb restored to sPreOverlayVerb
```

Triggers:

- `session.started` → fire `WAKING` (1 s)
- `notification` matching "Claude needs " → fire `ATTRACTING_ATTENTION`
  (1 s)

If a second overlay fires while one is playing: queue and fire on
expiry (last write wins, like the current `Personality::request`
queue).

### Verb-state struct

```cpp
struct VerbState {
  Verb     current;
  uint32_t entered_at_ms;
  uint32_t linger_until_ms;       // 0 = no linger active
  Verb     pre_overlay_verb;      // for WAKING / ATTRACTING_ATTENTION
  uint32_t overlay_until_ms;      // 0 = no overlay active
};
```

### Composition rule (rendering)

```cpp
Verb effective = state.overlay_active ? state.overlay_verb
               : state.current != NONE ? state.current
               : NONE;     // NONE means "show emotion face instead"
```

`FrameController::tick` becomes:

```cpp
if (effective_verb != NONE) {
  render_verb_face(effective_verb);    // dispatch on Verb
} else {
  Emotion snapped = snap(emotion.V, emotion.A);
  render_emotion_face(snapped);        // dispatch on Emotion enum
}
```

For motion, same rule: `MotionBehaviors` indexes its table by
`(verb, emotion)` — where `verb != NONE` selects the verb row and
`verb == NONE` falls through to the emotion row. Practically that
means doubling the row count of `kMotion[]`, but most rows can be
shared (verb rows usually want their motion regardless of mood).

## Trigger map (single source of truth)

This is the full event-handler table the new EventRouter implements.
Mirrors and supersedes `Personality::onAgentEvent`.

| `event.kind`        | Verb effect                                    | Emotion effect                                  |
|---------------------|------------------------------------------------|-------------------------------------------------|
| `session.started`   | fire `WAKING` overlay (1 s)                    | impulse `+0.6 V, +0.6 A` (push toward Excited)  |
| `session.ended`     | set `SLEEPING`                                 | (none — mood decays naturally)                  |
| `turn.started`      | set `THINKING` (clears overlay if any)         | (none — verb tells the story)                   |
| `activity.started`  | set `READING` / `WRITING` / `EXECUTING` per `activity.kind` | (none)                              |
| `activity.finished` / `.failed` | arm 1 s linger; on expiry → `THINKING` if no new verb | (none currently; future: failure → small −V impulse) |
| `turn.ended`        | clear verb (→ NONE)                            | impulse `+0.7 V, +0.9 A` (push toward Joyful)   |
| `notification` (matches "Claude needs ") | fire `ATTRACTING_ATTENTION` overlay (1 s) | (none)                       |
| (verb in `EXECUTING` ≥ 5 s) | self-transition to `STRAINING`         | (none yet)                                      |
| (verb in `STRAINING` ≥ 30 s) | (no verb change)                      | set persistent V target `-0.4` (stressed) until activity ends |
| (`pending_permission` set, polled) | (none)                          | set persistent V target `-0.6` until cleared    |
| (`pending_permission` cleared) | (none)                              | release the held V target (eases back to 0)     |

Persistent V targets stack by **largest magnitude** — if both
straining-stress (`-0.4`) and pending-permission (`-0.6`) are active,
the held target is `-0.6`.

## Module shape (proposed)

Three new modules replace `Personality`:

```
EmotionSystem.{h,cpp}   — owns Emotion struct, decay tick, V-target
                          stack, snap-to-discrete + hysteresis
VerbSystem.{h,cpp}      — owns VerbState struct, transition logic,
                          linger, overlay queue
EventRouter             — lives in robot_v2.ino setup wiring; maps
                          AgentEvents callbacks to {Verb, Emotion}
                          mutations per the trigger table above
```

Public surface roughly:

```cpp
namespace EmotionSystem {
  void begin();
  void tick();                                  // decay step

  void impulse(float dV, float dA);
  void setHeldTarget(uint8_t driver_id, float target_v);
  void releaseHeldTarget(uint8_t driver_id);

  Emotion raw();                                // (V, A) right now
  Emotion snapped();                            // nearest enum + V/A
}

namespace VerbSystem {
  void begin();
  void tick();

  void setVerb(Verb v);                         // continuous verb
  void clearVerb();                             // → NONE
  void armLinger(uint32_t ms);                  // 1s post-activity etc.
  void fireOverlay(Verb v, uint32_t duration_ms);

  Verb current();                               // raw
  Verb effective();                             // overlay-or-current
}
```

The .ino's onAgentEvent fans out to both — keeps the trigger-table
declarative and visible in one place rather than scattered through two
modules. (Once stable, it can move into a dedicated `EventRouter.cpp`
if the .ino gets too crowded.)

## Migration from `Personality`

The two systems together cover everything `Personality` does now,
plus more nuance. Mapping current-state → new world:

| Current state      | Verb           | Emotion driver                    |
|--------------------|----------------|-----------------------------------|
| `IDLE`             | NONE           | (V≈0, A≈0) snaps to Neutral       |
| `THINKING`         | THINKING       | n/a                               |
| `READING`          | READING        | n/a                               |
| `WRITING`          | WRITING        | n/a                               |
| `EXECUTING`        | EXECUTING      | n/a                               |
| `EXECUTING_LONG`   | STRAINING      | (after 30s also sets V target -0.4) |
| `FINISHED`         | NONE           | high V high A, snaps to Joyful    |
| `EXCITED`          | NONE           | mid-V mid-A, snaps to Excited     |
| `READY`            | NONE           | low-A high-V, snaps to Happy      |
| `WAKING`           | WAKING (overlay) | n/a                             |
| `SLEEP`            | SLEEPING       | n/a                               |
| `BLOCKED`          | NONE           | persistent V target -0.6 (Sad)    |
| `WANTS_ATTENTION`  | ATTRACTING_ATTENTION (overlay) | n/a                |

The protected `min_ms` windows on FINISHED / WAKING / WANTS_ATTENTION
disappear. They're replaced by:

- FINISHED's protection is now just "high A takes time to decay" — a
  pre-empting verb that arrives in the first second after `turn.ended`
  *will* take the screen, but the underlying emotion is still joyful;
  if the verb finishes quickly, the joyful face returns.
- WAKING / ATTRACTING_ATTENTION protection becomes the overlay
  duration — they always play in full because the overlay slot can't
  be cleared by a non-overlay event.

## Tuning surface

Everything you'd want to tune in one header:

```cpp
namespace EmotionSystem::Config {
  constexpr float kTauMs_A = 6000.0f;
  constexpr float kTauMs_V = 90000.0f;
  constexpr float kSnapHysteresisDist = 0.05f;
  constexpr uint32_t kSnapHysteresisHoldMs = 100;

  // Trigger impulses
  constexpr float kSessionStartedV = 0.6f, kSessionStartedA = 0.6f;
  constexpr float kTurnEndedV      = 0.7f, kTurnEndedA      = 0.9f;

  // Persistent targets
  constexpr float kBlockedV  = -0.6f;
  constexpr float kStrainV   = -0.4f;

  // Named emotion coordinates
  constexpr Coord kEmotions[] = {
    {  0.0f,  0.0f },     // Neutral
    {  0.5f,  0.2f },     // Happy
    {  0.6f,  0.6f },     // Excited
    {  0.9f,  0.9f },     // Joyful
    { -0.6f,  0.1f },     // Sad
  };
}

namespace VerbSystem::Config {
  constexpr uint32_t kLingerMs       = 1000;
  constexpr uint32_t kStrainDelayMs  = 5000;
  constexpr uint32_t kStrainStressDelayMs = 30000;  // when STRAINING also sets V target
  constexpr uint32_t kWakingMs       = 1000;
  constexpr uint32_t kAttentionMs    = 1000;
}
```

## Open questions / things to figure out on hardware

- **Snap-to-emotion vs blend.** Snap first (this design). When the
  discrete jumps look ugly, switch `kBaseTargets[]` from per-state
  rows to a 4-corner blend (V × A → bilinear-interpolated FaceParams).
  The trigger logic doesn't change — only the rendering.
- **Verb modulation by emotion.** Deferred. The hooks are easy to add
  later: `render_verb_face(verb)` becomes `render_verb_face(verb, snapped)`
  and the verb's `kBaseTargets` row is offset by emotion-derived deltas
  (e.g. small frown over the executing eyes when V is negative).
- **Whether `ATTRACTING_ATTENTION` should also nudge A.** Probably yes
  (a small `+0.3 A` impulse) — the robot is alerting *because* the
  agent is asking, so a little arousal feels right. Add if it feels
  flat in testing.
- **Failure as a V driver.** `activity.failed` currently does nothing
  emotionally. Small `-0.2 V` impulse on failure could give a "ugh"
  tell. Wire after the base system works.
- **Multiple persistent V drivers** with different held targets:
  current proposal is "largest magnitude wins". Alternative: weighted
  average. Largest-magnitude is simpler and matches the intuition
  that the most upsetting thing dominates the mood.
