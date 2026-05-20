# Motion — servo HAL and arm behaviours

The robot has a single hobby servo (SG92R) on `SERVO_PIN` driving the
arm/antenna. Two layers manage it:

- **`hal/Motion`** — low-level driver, layered control modes (jog, waggle,
  hold, etc. still available for bridge/tests).
- **`hal/MotionBehaviors`** — each frame, reads **effective** arm fields from
  `Face::effectiveFaceParams()` and drives the base emotion-arm layer.

Face body bob does **not** integrate a separate phase from arm period. After
the servo is commanded, `FrameController` maps **live arm offset** to vertical
bob — see [`FRAME_CONTROLLER.md`](FRAME_CONTROLLER.md).

## Effective params and loop order

Arm timing and range use the same blended + verb-overridden row as face
geometry (`arm_min_deg`, `arm_max_deg`, `arm_period_ms`, `arm_interval_ms`).

```text
SceneContextFill → tickEffectiveParams → MotionBehaviors::tick → Motion::tick → Face::tick
```

`tickEffectiveParams` (`src/face/FrameEffective.cpp`) smooths toward the active
expression's `kBaseTargets` row, samples `kVerbTimelines`, and combines with
`combineEmotionVerbFace`. `MotionBehaviors` must run **after** that step.

## Coordinate system

All angles in the public API are **offsets from centre** in degrees
(`-X` = one direction, `+X` = the other). Centre is mechanical 90°.

`MotionBehaviors::begin()` calls `Motion::setSafeRange(-45, +45)` to
clamp future targets to ±45°. The hard maximum supported by `Motion`
is ±90°.

## Motion driver layers

`Motion` runs layered control modes. Higher layers pre-empt
lower layers:

| Priority | Layer                                 | API                                 |
|----------|---------------------------------------|-------------------------------------|
| 5 (top)  | Hold (eased target + locked window)   | `holdPosition(deg, durationMs)`     |
| 4        | Jog (one-shot eased move)             | `playJog(deg, durationMs=250)`      |
| 3        | Pattern (5-frame keyframe waggle)     | `playWaggle(centre, amp, periodMs)` |
| 2        | Thinking sine drift                   | `setThinkingMode(on, …)`            |
| 1 (base) | Emotion arm sweep                     | `syncEmotionArmLayer(on, …)`        |

The base layer only writes the servo when nothing higher is active.
Bridge `set_servo_position` uses **Hold** and pre-empts the sweep.

### Emotion arm sweep (base layer)

`syncEmotionArmLayer(enable, minDeg, maxDeg, periodS, intervalS)` sweeps
between `minDeg` and `maxDeg` with period `periodS` and dwell `intervalS`
between half-cycles.

`MotionBehaviors::tick` always enables this layer and passes values from
`effectiveFaceParams()`:

- `arm_min_deg`, `arm_max_deg` — sweep range (degrees offset from centre).
- `arm_period_ms` — full sweep period in **milliseconds** (clamped to ≥ 50 ms).
- `arm_interval_ms` — dwell between sweeps in ms (≥ 0).

For **emotion** expressions, min/max/period/interval are **barycentric-blended**
across the triangulation like eye and mouth fields (`EmotionBlend::blendThree`
on the full `FaceParams` row).

For **verbs**, the same four fields come from the verb timeline overrides on
top of the (usually zero) verb `kBaseTargets` row.

`Motion::currentOffsetDeg()` returns the **commanded** angle minus centre. The
face simulator and `FrameController::bodyBobFor` use this for vertical bob.

`periodMsFor` / `periodMsForContext` are stubbed to **0** — body bob no longer
reads arm period. Do not use them for new code.

### Hold, jog, pattern, thinking (legacy / bridge)

These APIs remain for diagnostics and one-off behaviours:

- **`holdPosition`** — bridge raw servo tests.
- **`playJog`** — eased one-shot moves.
- **`playWaggle`** — 5-keyframe pattern (not driven by `kMotion[]` anymore).
- **`setThinkingMode`** — sine drift overlay (not table-driven per expression).

There is no per-expression `MotionMode` table in `FACE_CONFIG_DATA.h` after
schema v3.

## Tuning arm motion

Edit the last four cells of each emotion row in `kBaseTargets` (editor
**Arm** section on emotion anchors, or generated `FACE_CONFIG_DATA`).

For verbs, add keyframe overrides for `arm_min_deg`, `arm_max_deg`,
`arm_period_ms`, `arm_interval_ms` in `kVerbTimelines`. Shipped verb base rows
are `FACE_ROW_EMPTY` (zeros) so verb arm motion starts untuned until you
set timeline overrides.

## Tuning body bob

Bob **amplitude** comes from `kIdleAnim[expression].bob_amplitude_px`, or
`BOB_AMP_FOLLOW_EMOTION_ARM` to derive amplitude from the effective arm span.

Bob **position** each frame:

```text
t = clamp((currentOffsetDeg - arm_min_deg) / (arm_max_deg - arm_min_deg), 0, 1)
face_y bob offset ∝ (2*t - 1) * amplitude
```

So the face rides the arm angle; changing `arm_period_ms` changes how fast the
bob moves, not a separate face phase clock.

## Tuning notes

- Default ±45° safe range is conservative; widen only if mechanics allow.
- Bridge `set_servo_position` overrides via `Motion::holdPosition()` for
  limit testing without reflashing.
- `setEnabled(false)` disables motor writes (`motors_disabled` setting).
