# Motion Behaviors

Per-`Personality::State` table of motor recipes. The single tuning
surface for arm motion in the firmware: change a row in `kMotion[]` in
`robot_v2/MotionBehaviors.cpp` and reflash; nothing else needs to know.

## Pipeline position

```
Personality::current()  →  MotionBehaviors::tick()  →  Motion::play*  →  servo PWM
                                                          ▲
                                                    Motion::tick() slews
```

`MotionBehaviors::tick()` runs every loop. On state change it calls
`onEnter(s)` to seed the chosen motion mode. While the state holds it
calls `onDuring(s)` to fire the next periodic event (next waggle, next
oscillation leg, next drift target). All angles are clamped to the safe
range (`±45°`) by `Motion::setSafeRange` in `MotionBehaviors::begin`.

## Motion modes

| Mode           | Behaviour                                                                                         |
|----------------|---------------------------------------------------------------------------------------------------|
| `NONE`         | No motion. Cancels thinking-mode and any in-flight pattern on entry.                              |
| `STATIC`       | Slew once to `center` over `slewMs` (default 250 ms), then still.                                 |
| `RANDOM_DRIFT` | Pick a random offset in `[center-amp, center+amp]`, slew there over `slewMs` (default 500 ms), hold for `random(periodMs, periodMs+jitterMs)`, repeat. Reads as "alive but unhurried". |
| `OSCILLATE`    | Two-point ping-pong between `center-amp` and `center+amp`. `periodMs` = full cycle (each leg = half). `slewMs` = per-leg slew (`0` → continuous = half-period). |
| `WAGGLE`       | 5-frame waggle (centre → +amp → -amp → +amp → centre), retriggered every `periodMs`.              |
| `THINKING`     | Smooth sine around `center` with ±`amplitude` over a full period of `periodMs`. Eases in over the first second on entry. |

`THINKING` is sticky — it's the only mode that leaves
`Motion::setThinkingMode(true)` set; every other mode turns it off on
entry so the sine wave doesn't bleed across.

## Per-state table (current)

Field order: `{ mode, center, amplitude, periodMs, jitterMs, slewMs }`.

| State              | Mode           | center | amp | periodMs | jitter | slewMs | Feel                              |
|--------------------|----------------|--------|-----|----------|--------|--------|-----------------------------------|
| `IDLE`             | `RANDOM_DRIFT` | -20    | 5   | 5000     | 5000   | 500    | slow drifts to one side, every 5–10 s |
| `THINKING`         | `THINKING`     | -15    | 5   | 2000     | 0      | —      | gentle sine around centre         |
| `READING`          | `STATIC`       | -8     | —   | —        | —      | —      | small lean on entry, then still   |
| `WRITING`          | `OSCILLATE`    | 5      | 4   | 840      | 0      | 250    | head-nod-while-typing             |
| `EXECUTING`        | `OSCILLATE`    | -5     | 5   | 1000     | 0      | 0      | calm continuous swing             |
| `EXECUTING_LONG`   | `OSCILLATE`    | 0      | 5   | 750      | 0      | 0      | same shape, faster — "still working, getting antsy" |
| `FINISHED`         | `WAGGLE`       | 0      | 15  | 900      | 0      | —      | celebratory waggle every 900 ms   |
| `EXCITED`          | `OSCILLATE`    | -10    | 5   | 1000     | 0      | 0      | post-finished energy              |
| `READY`            | `RANDOM_DRIFT` | -15    | 8   | 2000     | 1000   | 500    | calmer drift, faster cadence than IDLE |
| `WAKING`           | `STATIC`       | 18     | —   | —        | —      | —      | startle: snap toward one extreme  |
| `SLEEP`            | `OSCILLATE`    | -20    | 5   | 8000     | 0      | 0      | very slow breath                  |
| `BLOCKED`          | `NONE`         | —      | —   | —        | —      | —      | intentionally still               |
| `WANTS_ATTENTION`  | `WAGGLE`       | 0      | 15  | 900      | 0      | —      | short waggle burst                |

The table in code is the source of truth — this doc may drift; check
`robot_v2/MotionBehaviors.cpp` if you need exact current values.

## Face sync (`periodMsFor`)

`MotionBehaviors::periodMsFor(state)` exposes the motor period for any
state with a stable phase (`OSCILLATE`, `WAGGLE`, `THINKING`). Returns
`0` for `RANDOM_DRIFT`, `STATIC`, `NONE`. `FrameController` uses this to
sync a small vertical face-bob to the arm's swing — when the motor is
at its "up" extreme the face bobs up too, so face and arm read as one
body's rhythm. Per-state body-bob amplitudes live in `bodyBobFor` in
`FrameController.cpp`.

If you change a state's `periodMs` here, the face bob automatically
re-syncs — that's the whole point of pulling it from this module.

## Safe range

`Motion::setSafeRange(-45, +45)` is set in `MotionBehaviors::begin`.
Every `Motion::play*` clamps to that range before driving the servo, so
a typo in this table cannot drive the arm into something solid. Adjust
`kSafeMin` / `kSafeMax` at the top of `MotionBehaviors.cpp` if your
chassis allows wider travel.

## Adding a state

The compile-time
`static_assert(sizeof(kMotion) == kStateCount * sizeof(StateMotion))`
ensures you can't forget to add a row when you add a state to
`Personality::State`. Add the row in the same enum order as the others.
