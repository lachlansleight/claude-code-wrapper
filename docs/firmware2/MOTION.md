# Motion — servo HAL and arm behaviours

The robot has a single hobby servo (SG92R) on `SERVO_PIN` driving the
arm/antenna. Two layers manage it:

- **`hal/Motion`** — low-level driver, layered control modes.
- **`hal/MotionBehaviors`** — table-driven mapping from `Expression` to
  motion mode. Reads `ctx.base_emotion_arm` for emotion mode (the
  blended preset from `EmotionBlend`).

`FrameController` reads `MotionBehaviors::periodMsForContext(ctx)` so
the face body-bob stays in lockstep with the arm — see
[`FRAME_CONTROLLER.md`](FRAME_CONTROLLER.md).

## Coordinate system

All angles in the public API are **offsets from centre** in degrees
(`-X` = one direction, `+X` = the other). Centre is mechanical 90°.

`MotionBehaviors::begin()` calls `Motion::setSafeRange(-45, +45)` to
clamp future targets to ±45°. The hard maximum supported by `Motion`
is ±90°.

## Motion driver layers

`Motion` runs five layered control modes. Higher layers pre-empt
lower layers:

| Priority | Layer                                 | API                                 |
|----------|---------------------------------------|-------------------------------------|
| 5 (top)  | Hold (eased target + locked window)   | `holdPosition(deg, durationMs)`     |
| 4        | Jog (one-shot eased move)             | `playJog(deg, durationMs=250)`      |
| 3        | Pattern (5-frame keyframe waggle)     | `playWaggle(centre, amp, periodMs)` |
| 2        | Thinking sine drift                   | `setThinkingMode(on, …)`            |
| 1 (base) | Emotion arm blend                     | `syncEmotionArmLayer(on, …)`        |

The base layer only writes the servo when nothing higher is active.
Thinking sine drift coexists with emotion-arm output (it adds a sine
modulation into the live target) but is suppressed by jog/pattern/hold.

### Hold

`holdPosition(deg, durationMs)` is the single test/diagnostic entry
point: it eases to the target, then locks the position for the
duration. Used by the `set_servo_position` raw control frame from the
bridge. `consumeHoldExpired()` reports the trailing edge so callers
can clean up (e.g. release the lock state in higher-level UI).

### Jog

`playJog(deg, durationMs)` is an eased one-shot move from the
servo's current position to the target via `smoothstep01()`. ~50 Hz
servo writes. Pre-empts patterns. Used internally and by some
overlays.

### Pattern

`playWaggle(centre, amplitude, periodMs)` plays a 5-keyframe
back-and-forth waggle centred at `centre`. Total play time is
roughly `periodMs / 2`. This is the `WAGGLE` motion mode used by
some verbs/overlays.

### Thinking sine drift

`setThinkingMode(on, centerOffset=0, amplitude=5, periodMs=2000)` adds
a continuous sine drift on top of the active layer. It fades in over
~1 s so engaging it doesn't snap the servo.

### Emotion arm

`syncEmotionArmLayer(enable, minDeg, maxDeg, periodS, intervalS)` is
the lowest-priority continuous mode, used while no verb is active.
Inputs come from `EmotionBlend::blendedEmotionArmMotion(v, a)`:

- `minDeg`, `maxDeg` — sweep range.
- `periodS` — sweep period.
- `intervalS` — dwell between sweeps.

The renderer uses the period directly for the body-bob phase, so the
face nods in time. `resetEmotionArmPhase()` snaps the cycle to the
beginning at `minDeg` — useful when transitioning into emotion mode
from a non-arm state.

`setEnabled(false)` disables motor writes globally, used by the
`motors_disabled` setting from the bridge `config_change` frame.

## MotionBehaviors

`MotionBehaviors::tick(const Face::SceneContext& ctx)` is called every
loop iteration, after `EventRouter::tick()` has updated the verb state
and `SceneContextFill` has produced a fresh `ctx`.

### Emotion expressions vs verbs

```cpp
if (FaceConfig::isEmotionExpression(ctx.effective_expression)) {
    Motion::syncEmotionArmLayer(true, ctx.base_emotion_arm.…);
} else {
    // verb / overlay — drive from kMotion[expression]
    Motion::syncEmotionArmLayer(false, …);
    applyMotionRow(kMotion[expression], onEnter, onDuring);
}
```

For emotion expressions, the arm preset is *blended* across the
emotion triangulation, so transitioning from `Happy` → `Excited` is a
smooth period and range crossfade rather than a discrete jump.

For verbs and overlays, the arm preset is read from the static table
`kMotion[]` in `FACE_CONFIG_DATA.h`, indexed by `Expression`.

### Motion modes (`kMotion`)

```cpp
enum class MotionMode : uint8_t {
    NONE,            // do nothing
    STATIC,          // hold a fixed offset
    RANDOM_DRIFT,    // random walk with jitter
    WAGGLE,          // periodic 5-frame pattern (calls playWaggle)
    THINKING_SINE,   // continuous sine drift (calls setThinkingMode)
};
```

Each row in `kMotion[]` is `{ mode, params… }`. `MotionBehaviors`
detects expression changes by comparing against the previous tick and
calls the row's `onEnter` action (e.g. start a waggle, switch to
thinking mode), then per-tick checks `consumeHoldExpired()` and runs
the `onDuring` action if defined.

### `periodMsFor(expression)` / `periodMsForContext(ctx)`

These return the active arm period in milliseconds — for emotion mode
that's `ctx.base_emotion_arm.period_s * 1000`, for verbs it's the
period from `kMotion[expression]`. `FrameController` calls
`periodMsForContext(ctx)` and integrates it into `sBodyBobPhaseRad`.
This is why changing `period_ms` in `kMotion[]` automatically resyncs
the face — there's no separate face configuration that needs to match.

## Tuning notes

- The default ±45° safe range is conservative. Increase if you've
  verified the servo + arm geometry can clear the rest of the body.
- `THINKING_SINE` adds *on top of* whatever the base layer is writing,
  so a verb that uses `THINKING_SINE` plus a small `STATIC` centre
  offset gets gentle drift around that centre rather than at zero.
- `RANDOM_DRIFT` produces gentle random walks rather than jerky jumps;
  it's the right mode for an "alive but doing nothing" verb (and is
  the default for `VerbThinking`).
- The bridge can override the servo with the `set_servo_position`
  control frame — `EventRouter` translates it to
  `Motion::holdPosition()`. Useful for testing servo limits without
  reflashing.
