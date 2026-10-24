# Engine: what exists, what to port, what to fix

> **Status (2026-10-24):** the plan in [`PLAN.md`](PLAN.md) compiles the
> firmware C++ directly instead of porting it, so **§1, §3, §4, §7 and §8**
> (the TS engine, driven mode, composition parity, pixel-buffer sprite,
> JavaScriptCore) are superseded. They are kept because they document real
> gaps in the face editor's engine. **§2 (firmware behaviour layer), §5
> (arms), §6 (frame rate) and §9 (stale docs) remain the reference** and are
> what the host harness is verified against.

This covers the TS face engine as it stands, the firmware behaviour layer that
needs porting, and the changes needed so the Mac app behaves exactly like
`robot_v3`.

Everything here was checked against the code at commit `1094a48`. **Where the
docs and the code disagree, trust the code.** Section 9 lists the stale docs.

---

## 1. The TS face engine today

**Location:** `face-editor/src/app/_lib/face-engine/`. The folder's rule is no
imports from `react` or `next/*` (see that folder's `README.md`).

| File | Lines | Role |
|---|---|---|
| `faceRenderer.ts` | 468 | 1:1 port of `robot_v3/src/face/FaceRenderer.cpp`. Draws eyes, mouth, effects and mood ring into a 240×240 sprite. Geometry: `kCx=120, kCy=120, kEyeY=95, kEyeLX=85, kEyeRX=155, kMouthY=165, kPivotY=130`. |
| `frameController.ts` | 1244 | Port of `FrameController.cpp`: tweens, blink, gaze, breathing, body bob, verb timelines, static/blend modes, arm overlay. Text-stream and progress-fade paths are omitted. |
| `emotionBlend.ts` | 326 | Barycentric blend of face parameters and idle animation in (valence, arousal) space. |
| `emotionTriangulationLive.ts` | 100 | Builds the Delaunay mesh from `kEmotionPoints` at load. The firmware uses the pre-generated `EmotionTriangulation.h` instead. |
| `verbTimeline.ts` | 325 | Keyframed verb override tracks, plus the 500 ms cross-fade. **Holds its state at module level**, so there can be only one face per JS context. |
| `sceneFaceCombine.ts` | 86 | Combines the emotion layer with the verb layer, and handles smoothing. |
| `emotionArmSim.ts` / `emotionArmBob.ts` | 113 / 59 | Arm sweep simulation, and body bob derived from the arm angle. |
| `effectsRenderer.ts` | 215 | Procedural read/write "code stream" effects. Uses only `fillRect`. |
| `tftSprite.ts` | 111 | A browser stand-in for the TFT_eSPI sprite, built on `<canvas>`. |
| `robotSettings.ts` | 44 | Named colour palette; mirrors `Settings.cpp` defaults. |
| `FACE_CONFIG_DATA.ts` + `.snapshot.json` | 1695 / 86 KB | Generated config data. Schema v3: 22 expressions, 13 named emotions, 28-field parameter rows. |

### Loading the config

The editor loads config through `GET /api/loadFaceConfig` →
`loadFaceConfigFromDisk`. That uses the snapshot JSON if present; otherwise it
uses `FACE_CONFIG_DATA.ts` plus a Delaunay mesh computed at load.

The Mac app should bundle the snapshot (or import the `.ts`) at build time.

Saving in the editor regenerates the `.ts`, `.h`, `EmotionTriangulation.h` and
`.snapshot.json` from one source, using the code in
`face-editor/src/app/_lib/face-config-codegen/`. Adding another output (for
example a Swift file) would be easy.

### Browser dependencies

Only two files touch browser APIs:

- **`tftSprite.ts`:** `document.createElement("canvas")` and
  `CanvasRenderingContext2D`.
- **`frameController.ts`:**
  - `start(canvas: HTMLCanvasElement)` (around line 940);
  - `requestAnimationFrame` / `cancelAnimationFrame` (around lines 374 and 960);
  - `performance.now()` (around line 367);
  - `pushSpriteToCanvas` (around line 545);
  - `drawArmOverlay` (around line 567).

Everything else is pure math.

**The sprite methods the renderer uses:** `fillSprite`, `fillScreen`, `fillRect`,
`drawLine`, `fillEllipse`, `drawEllipse`, `fillCircle`, `drawCircle`,
`fillSmoothCircle`. That's a small software rasteriser's worth of work (§7).

**What's still missing for this goal:** the TS engine has no WebSocket
connection and no event handling. The editor's log replay (`logReplay.ts`)
sends events back to the bridge, so the *robot* does all the event-to-behaviour
work. Nothing in TS reacts to agent events.

---

## 2. The firmware behaviour layer (to port)

**Files:**
- `robot_v3/src/app/EventRouter.cpp`
- `robot_v3/src/behaviour/VerbSystem.cpp`
- `robot_v3/src/behaviour/EmotionSystem.cpp`
- `robot_v3/src/agents/AgentEvents.cpp`
- `robot_v3/src/app/SceneContextFill.cpp`
- `robot_v3/src/face/FrameEffective.cpp`

### 2.1 Main loop order (`robot_v3/robot_v3.ino`, around line 90)

```
Bridge::tick()                 // read WS frames, dispatch synchronously
EventRouter::tick()            // AgentEvents::tick → session poll → VerbSystem::tick
                               //   → held-driver maintenance → EmotionSystem::tick
SceneContextFill::fill(ctx)    // snapshot of behaviour state for the face
Face::tickEffectiveParams(ctx, now)
MotionBehaviors::tick(ctx); Motion::tick()   // servo
Face::tick(ctx)                // render (throttled to 33 ms / 16 ms)
```

**Message path:**
1. `BridgeClient` parses the JSON.
2. `EventRouter::onBridgeMessage` calls, in order:
   `dispatchRawCommand` → `AgentEvents::dispatch` → `BridgeControl::dispatch`.
3. For `type == "agent_event"`, `AgentEvents` runs the session latch filter
   (§2.6), updates its `AgentState` singleton, then calls
   `EventRouter::onAgentEvent`.

Messages are applied **in order, before** that loop iteration's tick. One hook
can fan out into several events (for example, Stop → `message.assistant` then
`turn.ended`); they're all applied before anything ticks.

### 2.2 The face pulls; nothing pushes

The behaviour layer never calls into the face. Each loop,
`SceneContextFill::fill` builds a `Face::SceneContext` snapshot, and the face
reads it.

| `SceneContext` field | Source |
|---|---|
| `effective_expression` | If `VerbSystem::effective() != None`: `kVerbToExpression[verb]`. Otherwise the expression for the snapped emotion, which in practice is always Neutral (§2.5). |
| `verb_time_in_current_ms` | `VerbSystem::timeInEffectiveMs()`; the playhead for the verb timeline |
| `base_face_params` | `EmotionBlend::blendedFaceParams(raw.v, raw.a)` |
| `mood_v`, `mood_a` | `EmotionSystem::raw()` |
| `read_tools_this_turn`, `write_tools_this_turn` | `AgentState`; used by the activity dots |
| colours, `settings_version`, `render_mode` | `Settings` |

`FrameEffective::tickEffectiveParams` then:
1. smooths `base_face_params` toward the target with τ = 250 ms
   (`emotion_geometry_smooth_tau_ms`);
2. samples the verb timeline at `verb_time_in_current_ms`;
3. combines the two (`combineEmotionVerbFace`).

The mood ring is **not** a separate input. It's the `ring_r/g/b` fields of the
combined parameters, smoothed in `FrameController` with τ = 200 ms.

**Verb → expression** (`robot_v3/src/face/VerbEnum.generated.h`):

| Verb (slug) | Expression |
|---|---|
| `none` | Neutral |
| `thinking` | VerbThinking |
| `reading` | VerbReading |
| `writing` | VerbWriting |
| `executing` | VerbExecuting |
| `straining` | VerbStraining |
| `sleeping` | VerbSleeping |
| `waking` | VerbWaking |
| `attracting_attention` | VerbAttractingAttention |
| `celebrating` | VerbCelebrating |

TS has the `Expression` enum but not these two tables, so copy them.

### 2.3 Event → behaviour (`EventRouter.cpp` `onAgentEvent`, around line 54)

These apply only to events that pass the session latch (§2.6).

| `event.kind` | Verb | Emotion |
|---|---|---|
| `session.started` | `fireOverlay(Waking, 1500, None)` | `setValence(+0.6)`, `setArousal(+0.6)`: sets absolute values, not an impulse |
| `session.ended` | `setVerb(Sleeping)` | — |
| `turn.started` | If the current verb is Sleeping: `fireOverlay(Waking, 1500, Thinking)`. Otherwise `setVerb(Thinking)`. | — |
| `activity.started` | `activity.kind == "shell.exec"` → Executing. Write-type kinds (`file.write`, `file.delete`, `notebook.edit`) → Writing. **Everything else → Reading**, including `shell.background`. | — |
| `activity.finished` / `activity.failed` | `armLinger(1000)` | — |
| `turn.ended` | `fireOverlay(Celebrating, 3000, None)` | `impulse(+0.7, +0.9)` |
| `notification` with text starting `"Claude needs"` (case-sensitive) | `fireOverlay(AttractingAttention, 1500)`. Two-argument form: after the overlay, it returns to whatever verb was current when it fired. | — |
| everything else (`message.*`, `thinking`, `todo.updated`, `subagent.*`, `unknown`, …) | — | Only updates text/debug state |

**Polled state** (`EventRouter::tick`, around line 217):
- **Pending permission:** while `AgentState.pending_permission` is non-empty,
  held driver 1 targets valence −0.6; it's released when the permission clears.
- **Long strain:** while the current verb is Straining and
  `timeInCurrentMs ≥ 30000`, held driver 2 targets valence −0.4; it's released
  otherwise.

**Classifier quirk:** `classifyActivity` can mark a shell command that looks
like it writes as WRITE. But `EventRouter` checks `shell.exec` first, so shell
commands are always Executing. The WRITE classification only affects the
per-turn read/write counters used by the activity dots.

### 2.4 VerbSystem rules (`VerbSystem.cpp`)

**State:** current verb, `enteredAt`, `lingerUntil`, plus the overlay (verb,
post-verb, start time, end time, blend-out time). `begin()` starts in
**Sleeping**.

- **`setVerb(v)`**
  - Cancels any active overlay immediately, sets the current verb, resets
    `enteredAt`, and clears the linger.
  - **Setting the same verb again still restarts its time**, which restarts
    the timeline phase and the strain timer.
  - `setVerb(None)` is the same as `clearVerb()`.
- **`fireOverlay(v, ms[, post])`**
  - The duration is clamped to at least 501 ms (`kVerbTransitionDurMs` 500 + 1).
  - The post-verb is the explicit argument, or, in the two-argument form, the
    current verb at the moment it fires.
  - A new overlay replaces one that's in flight. Firing an overlay does **not**
    clear the linger.
- **`effective()`**
  - Returns the overlay verb until `until − 500 ms`, then the post-verb. So the
    500 ms cross-fade happens *inside* the overlay's duration.
- **`timeInEffectiveMs()`**
  - During an overlay, it's the time since the overlay started; this includes
    the blend-out phase, where the post-verb is already showing.
  - After the overlay ends, it's the time since `enteredAt`, which was reset
    when the overlay ended.
- **`tick()`**, in this order:
  1. If the overlay has expired: apply the post-verb, reset `enteredAt`, clear
     the linger, and **return early** for that frame.
  2. If the linger has expired: if the current verb isn't None or Sleeping, it
     becomes Thinking and `enteredAt` resets. This happens even if it's already
     Thinking, and even while an overlay is active.
  3. Strain promotion: if the verb is Executing and has been for ≥ 5000 ms
     (`kVerbSim.strain_delay_ms`), it becomes Straining. The linger doesn't
     stop this.
- **Nothing is protected or queued.** For example, a `turn.started` during
  Celebrating cuts the overlay off immediately. The face still fades over
  500 ms through the verb timeline's cross-fade.
- **There is no idle timeout to sleep.** The only ways into Sleeping are boot
  and `session.ended`. After `turn.ended`, the face plays Celebrating and then
  shows no verb (emotion face only) indefinitely.

### 2.5 EmotionSystem (`EmotionSystem.cpp`)

**Two layers:**
- **`goal`:** set by `impulse`, `setValence`, `setArousal` and `modify*`.
  Clamped to valence ∈ [−1, 1] and activation ∈ [0, 1].
- **`raw`:** follows `goal` with τ = **50 ms** (`tau_ms_raw_follow`). This is
  the value that drives the face.

**`tick()`** (does nothing if dt = 0):
- **Activation:**
  `newA = max(0.5, a + (0 − a)·(1 − e^(−dt/6000)))`, then
  `goal.a = min(goal.a, newA)`. Effects:
  - activation decays only from above, and stops at 0.5;
  - below 0.5 it **stays where it is** and never rises on its own;
  - `begin()` sets the goal to (0, 0), so activation sits at 0 until a
    `session.started` or `turn.ended`.
- **Valence:** relaxes toward the active held driver's target with τ = 90 s.
  The driver with the largest |target| wins; with no drivers, the target is 0.
- **Accumulation:** repeated `turn.ended` impulses (+0.7 valence) build up
  toward +1, because they decay so slowly.

**Snap:** nearest-anchor matching with hysteresis (0.05 distance, held for
100 ms). However, `dist_sq_tie_eps = -7.0` makes `d <= bestD + eps`
impossible, so `emotionForPoint` always returns Neutral. **In practice the snap
is disabled.** The expression comes entirely from the continuous blend at `raw`.
The TS config has the same −7.

**Held drivers:** 8 slots keyed by id. 1 = PendingPermission, 2 = Straining.

**Randomness:** none here. The only randomness is in FrameController (blink
schedule and idle glances).

**Config** (`kEmotionSim`, `FACE_CONFIG_DATA.h` around line 747; also present
in TS `FACE_CONFIG_DATA.ts`):

| Setting | Value |
|---|---|
| `tau_ms_activation` | 6000 |
| `tau_ms_valence` | 90000 |
| `tau_ms_raw_follow` | 50 |
| `snap_hysteresis_dist` | 0.05 |
| `snap_hysteresis_hold_ms` | 100 |
| `dist_sq_tie_eps` | −7.0 |
| `baseline_activation` | 0.5 |

### 2.6 Session latch (`AgentEvents.cpp` `latchFilter`, around line 239)

The robot shows **one session at a time** and keeps no per-session state.

- An event with no `session_id` passes only if nothing is latched.
- If nothing is latched, the first session seen gets latched.
- Events from other sessions are **dropped**, except `turn.started` and
  `session.started`, which **take over the latch**. In effect, the most recent
  turn start wins.
- A `session.ended` from the latched session unlatches it, but is still
  processed.
- **`active_sessions` frames:**
  - If nothing is latched, latch `ids[0]`. That's the session the bridge has
    tracked longest; it can be up to 10 minutes stale.
  - If the latched session isn't in the list, unlatch.
- While nothing is latched (and connected), the robot sends
  `{"type":"request_sessions"}` every 5 s.

For bounce detection the app needs to see completions across **all**
sessions, not just the latched one. See [`BRIDGE.md`](BRIDGE.md).

### 2.7 Disconnect behaviour (`AgentEvents::notifyConnection`)

On disconnect, the firmware clears the latch, the per-turn text state and
`pending_permission`, **but leaves VerbSystem and EmotionSystem alone**. A
disconnect in the middle of a tool leaves the face in Executing, then
Straining, then the sad driver kicks in. This is a robot gap; copy it as-is for
now.

### 2.8 Constants

| Constant | Value | Where |
|---|---|---|
| `kVerbLingerMs` | 1000 | `EventRouter.cpp` around line 18 |
| `kOverlayMs` (Waking, AttractingAttention) | 1500 | `EventRouter.cpp` |
| Celebrating duration | 3000 | inline in `onAgentEvent` |
| `kStrainStressDelayMs` | 30000 | `EventRouter.cpp` |
| `kBlockedV` / `kStrainV` | −0.6 / −0.4 | `EventRouter.cpp` |
| `kSessionPollMs` | 5000 | `EventRouter.cpp` |
| Valence/arousal literals | +0.6/+0.6 (session start), +0.7/+0.9 (turn end) | inline |
| `kVerbTransitionDurMs` | 500 | `robot_v3/src/face/VerbTimeline.h`; also in TS config |
| `kVerbSim.strain_delay_ms` | 5000 | `FACE_CONFIG_DATA.h`; `default_overlay_duration_ms` (1500) is unused |
| `kFrameAnim` | see §6 | `FACE_CONFIG_DATA.h` around line 757 |
| Bridge client heartbeat / reconnect | 15 s ping, 3 s timeout, 2 failures; 2 s reconnect | `robot_v3/src/bridge/BridgeClient.cpp` |

### 2.9 Hardware-specific parts to drop

- **Settings/NVS:** palette, face mode, motors disabled. Map them to app
  preferences, or drop them.
- **`BridgeControl`:** `setColor`, `config_change` (display mode, motors
  disabled), `set_servo_position`. Drop it, or keep only `setColor` for the
  palette.
- **WiFi, Provisioning, Display, SerialDebugging:** drop.
- **`SceneContextFill`:** the text sanitising and fixed-size character buffers
  are only for text mode.
- **Text and debug display modes** (`TextScene.cpp`, 381 lines): optional, and
  not needed for face mode.
- **`AgentEvents`:** pure logic apart from `millis()`. Port the latch, the
  `working` / `pending_permission` / per-turn counters and the classifier. Most
  of its 767 lines are text-mode string formatting.

### 2.10 Size and tricky semantics

About 2,280 lines of firmware (.cpp + .h) are involved, but only around 700
matter for the face.

**Faithful TS port, roughly:**

| Piece | Lines |
|---|---|
| VerbSystem | ~120 |
| EmotionSystem | ~120 |
| Latch, state and classifier | ~150 |
| Router | ~100 |
| Context fill | ~40 |
| WebSocket client glue | ~60 |
| Driven frame-controller mode (§3) | 60–100 |

**Things that are easy to get wrong:**
- **Same-verb `setVerb` restarts time.** Don't skip it as a no-op.
- **The post-verb playhead** uses the overlay's start time during blend-out,
  then jumps to 0 when the overlay ends.
- **`VerbSystem.tick` returns early** on the frame an overlay ends.
- **Linger can fire during an overlay**, but the post-verb captured when the
  overlay fired still wins when it ends.
- **Use `null` "not set" markers, not 0.** The firmware uses `0` to mean
  "unset" (for example `lingerUntil == 0`) and timer comparisons that would
  break when the millisecond clock wraps. JS has no wraparound, but
  `performance.now()` starts near 0, so 0 is a real timestamp there.
- **Module-level state:** the firmware uses `static` variables. Use closures
  or classes in TS. Remember that `verbTimeline.ts` already has module-level
  cross-fade state.
- **Keep these quirks:** the 0.5 activation floor (decays only from above),
  the disabled snap (or just hard-code Neutral), and the 50 ms raw follow.

---

## 3. Driven mode for `frameController.ts`

The TS frame controller is built to be controlled by the editor. It needs a
mode where it's driven the way the firmware drives the face.

**Proposed API:** `drive({ expression, verbTimeMs, moodV, moodA, readTools, writeTools })`,
called each tick, mirroring `FrameEffective.cpp` + `FrameController.cpp`.

What it does:
- **Emotion row:** `emotionBlend.blendedFaceParamsIndexed(moodV, moodA)`,
  smoothed with τ = 250 ms.
- **Verb layer:** `sampleEffectiveVerb(expression, t, verbTimeMs)`, then
  `combineEmotionVerbFace`.
- **Idle row** (blink, gaze, bob policy): `blendedIdleAnim(moodV, moodA)` when
  the expression is an emotion; otherwise `kIdleAnim[expression]`
  (`FrameController.cpp` `idleFor`, around line 73).
- **Breathing:** skipped for verb-timeline expressions, Joyful, Gleeful and
  VerbSleeping (`FrameController.cpp` around line 389). The firmware truncates
  the offset with an `(int16_t)` cast; TS uses `Math.round`.
- **Mood ring:** the smoothed `sMoodR/G/B` (τ = 200 ms), not the raw `ring_*`.
- **Code-stream alphas:** driven by the expression (§4).
- Blink, gaze, body bob and the arm simulation as in the current `tick()`.

### Current TS gaps, all verified

- **Normal mode** (`tick()`, around lines 655–670) targets the fixed
  `baseTargets[exprIdx]` row for emotion expressions. It only uses the blend
  at (v, a) under verb timelines. The firmware *always* uses the continuous
  blend.
  - Normal mode's idle row uses the expression's anchor, not (v, a).
- **Blend mode** (`tickBlend`, around line 807) does blend, but:
  - smoothing alpha is 1.0, so there's no 250 ms smoothing;
  - there's no blink or gaze;
  - breathing applies during verbs, which the firmware skips.
- **`setBlendVerbPreview(verb)`** does nothing if the verb is unchanged (around
  line 1144). The firmware restarts the verb's time on every `setVerb`.
- **Bug: `fireBlendVerbTransient(verb, ms, postVerb = null)`** (around line
  1168). Because of the `= null` default, `postVerb !== undefined` is always
  true, so omitting the post-verb always clears the verb layer. The firmware
  falls back to the current verb.
- **Verb time** comes from an internal `sVerbEnteredMs`, which is reset only on
  expression change. The driven mode should take `verbTimeMs` from the
  behaviour layer instead.

---

## 4. Screen composition parity

Compare `robot_v3/src/face/Scene.cpp` with `renderScene` in `faceRenderer.ts`:

| | Firmware | TS today |
|---|---|---|
| Draw order | background → face → **effects** → mood ring → **activity dots** | background → **effects** → face → mood ring |
| Code-stream effects | Read alpha fades toward 1 during VerbReading, write alpha during VerbWriting, with `effects_fade_ms` = 100 (`FrameController.cpp` around line 359) | Manual preview toggle only (`setStreamEffectPreview`) |
| Mood ring colour | Smoothed `sMoodR/G/B`, τ = 200 ms; skipped when all zero | Raw `p.ring_*` (TS computes the smoothed value but doesn't use it) |
| Activity dots | `ActivityDots.cpp` (65 lines): per-turn read/write counts, with `progress_fade_ms` = 280 | Not ported |
| Arm overlay | n/a (physical arms) | Drawn on the output canvas. Move this into the robot window; don't draw it on the screen |

---

## 5. Arms

**Good news:** `tickEmotionArmPhase` in `emotionArmSim.ts` already mirrors the
firmware's emotion-arm layer (`robot_v3/src/hal/Motion.cpp` `tick`, around
lines 175–215, and `MotionBehaviors.cpp`). It sweeps from `arm_min_deg` up to
`arm_max_deg` and back on a half sine over `arm_period_ms`, then holds at the
minimum for `arm_interval_ms`. It reads the *effective* (blended emotion plus
verb) parameter row.

- In normal use, this is the **only** thing that moves the servo. Motion's
  other modes (jog, hold, waggle, thinking drift) are reachable only through
  raw servo commands (`set_servo_position`, `/api/raw/servo/hold`).
- **Missing in TS:**
  - the firmware rounds to whole degrees (`lroundf` → `int8_t`);
  - it clamps to the safe range of ±45° (`MotionBehaviors.cpp`
    `kSafeMin`/`kSafeMax`).
- **Body bob** follows the *commanded* angle on the device too
  (`Motion::currentOffsetDeg()`), so the face will match.
- **For the robot window:** the gear mirrors one servo to both arms, so draw
  the right arm at +θ and the left at −θ about their pivots. The existing
  `drawArmOverlay` already mirrors it this way:
  `thetaRight = -offset; thetaLeft = π - thetaRight`.
- **Optional realism:** the real SG92R servo lags the commanded angle. A speed
  limit on the *drawn* arms (not on the bob) would mimic that.

---

## 6. Frame rate

The robot renders at a capped rate (`kFrameAnim`):
- `tick_interval_ms` = 33, i.e. ~30 fps;
- `tick_interval_stream_ms` = 16, i.e. ~60 fps, while Reading/Writing or while
  the code streams are still fading.

Tick the simulation at these rates for fidelity. Throttle the Dock tile further
if needed (see [`MACOS.md`](MACOS.md)). The other `kFrameAnim` values (blink
timings, breath period 4000 ms, and so on) are already in the TS config.

---

## 7. Pixel-accurate rendering

The editor's canvas `TFTSprite` is **not** pixel-identical to the device:

- **Canvas** anti-aliases `ellipse()` fills and half-pixel-offset `drawLine`
  strokes, and `imageSmoothingEnabled` is true.
- **TFT_eSPI** draws hard-edged pixels (Bresenham-style lines, scanline
  ellipses). Only `fillSmoothCircle`, used for the pupils, is smoothed.
- Colours on the device are 16-bit (RGB565). `rgb565ToCss` already reproduces
  that.

A pixel-buffer `TFTSprite` (a `Uint8ClampedArray` of 240×240×4) that rasterises
like TFT_eSPI would therefore be **more** faithful than the editor. It's also
exactly what's needed to run the engine without a browser.

Primitives to implement:

| Method | Notes |
|---|---|
| `fillSprite` / `fillScreen` | — |
| `fillRect` | — |
| `drawLine` | Bresenham |
| `fillEllipse` / `drawEllipse` | — |
| `fillCircle` / `drawCircle` | — |
| `fillSmoothCircle` | Anti-aliased edge |

Use TFT_eSPI's own algorithms as the reference.

The physical screen is a 240×240 **round** GC9A01 (`robot_v3/User_Setup.h`).
Mask the output to a circle.

---

## 8. Making it run without a browser (JavaScriptCore)

1. Replace `performance.now()` with an injected clock, and replace the
   `requestAnimationFrame` loop with an exported `tick(nowMs)` that the host
   calls.
2. Change the output from `pushSpriteToCanvas` to exposing the pixel buffer and
   the current arm angle. Move `drawArmOverlay` out of the engine.
3. Use the pixel-buffer `TFTSprite` (§7).
4. Bundle with esbuild into one IIFE exposing roughly
   `globalThis.robot = { onMessage(json), tick(nowMs), frame() }`.
   Embed `FACE_CONFIG_DATA.snapshot.json`.
5. Keep the editor working: the canvas `TFTSprite` and RAF loop can stay as
   the editor's way of running the same engine.

---

## 9. Docs that are out of date

These don't match the code; trust the code.

- **`CLAUDE.md` "Firmware quick-map"** describes the old robot_v2
  `Personality` system: `pending_permission` polling by Personality, protected
  `min_ms` windows, queuing, and FINISHED / WAKING / WANTS_ATTENTION states.
  **None of that exists in robot_v3**; there is no `Personality` in
  `robot_v3/src`.
- **`docs/firmware2/VERB_SYSTEM.md` and `BRIDGE_AND_EVENTS.md`:**
  - They say `turn.ended` clears the verb; the code plays a 3 s Celebrating
    overlay.
  - They call `session.started` an impulse; the code sets absolute values.
  - They say overlays last 1 s; the code uses 1.5 s.
  - They map `shell.background` to Executing; the code maps it to Reading.
  - They describe permission as event-driven; the code polls it.
  - Their verb list is out of order and omits Celebrating.
- **`docs/firmware2/EMOTION_SYSTEM.md`:**
  - says raw follow is ~300 ms; the code uses 50 ms;
  - lists 14 anchors; there are 13;
  - says activation decays to 0; it stops at 0.5 and only decays from above.
