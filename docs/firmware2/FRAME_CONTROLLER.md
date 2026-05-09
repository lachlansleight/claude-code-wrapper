# FrameController — the per-frame face pipeline

`FrameController` (`src/face/FrameController.{h,cpp}`) is the orchestrator
for everything visible on the display. It is the **only** place that owns
mutable face animation state. All renderers below it
(`FaceRenderer`, `EffectsRenderer`, `MoodRingRenderer`, `ActivityDots`,
`TextScene`) are stateless — they consume a fully-resolved `FaceParams`
plus a `SceneRenderState` derived state struct.

The public API is small:

```cpp
namespace Face {
  void begin();                          // seed RNG, init tweens
  void tick(const SceneContext& ctx);    // render at most one frame
  void invalidate();                     // force the next tick to render
  const FaceParams& baseTargetFor(Expression e);
}
```

`tick()` is cheap to call every loop iteration; throttling lives inside.

## What `tick()` does, step by step

1. **Settings invalidation.** If `Settings::settingsVersion()` has changed
   since the last frame (palette edit, face/text toggle), snap the smoothed
   emotion to the current target so the new palette/preset doesn't bleed in
   over the smoothing window.

2. **Expression-change edge.** When `ctx.effective_expression` differs from
   the last frame, run `onExpressionChange()`:
   - On **leaving `VerbThinking`**, normalize `face_rot` and `pupil_dx` by
     the current think-tilt sign so they snap to neutral on exit (otherwise
     you get a visible pop).
   - On **`Happy → Neutral`**, capture the current per-turn read/write tool
     counts so the activity-dot rings can fade out over ~280 ms while the
     live counters reset.
   - Schedule the next blink, reset think-tilt timing, reset the
     idle-glance state.

3. **Throttle gate.** Default tick interval is 33 ms (~30 Hz). When
   `read_stream_alpha` or `write_stream_alpha` is non-zero (i.e. a stream
   effect is fading in or visible), the gate drops to 16 ms (~60 Hz) to
   keep the scrolling tokens smooth. Returns early when not yet due.

4. **Stream-effect alphas.** Exponentially smooth `sTextStreamAlpha` and
   `sWriteStreamAlpha` toward 1.0 while the corresponding verb is active,
   else toward 0. τ ≈ 100 ms. These drive the read/write code-stream
   overlays.

5. **Emotion geometry smoothing.** Lerp every field of `sSmoothedEmotion`
   toward `ctx.base_face_params` using a τ ≈ 100 ms exponential filter
   (configurable via `kFrameAnim.emotion_geometry_smooth_tau_ms`).
   `ctx.base_face_params` already came out of `EmotionBlend` (continuous
   barycentric blend over the emotion triangulation) so this filter only
   has to absorb the verb-driven snap to a different preset family, not
   the per-frame jitter from the raw emotion point.

6. **Verb timeline sampling.** Call `sampleEffectiveVerb(effective_expr,
   now, time_in_verb_ms, hasField[], fieldVals[])` every frame
   regardless of whether the current expression is a verb. The sampler
   internally cross-fades over 500 ms when the effective verb changes
   (or when transitioning to/from a non-verb state) — see
   [`VERB_SYSTEM.md`](VERB_SYSTEM.md#verb-cross-fade) for the per-field
   blend rules and snapshot-on-retarget behavior. The unconditional call
   is required for the ramp-out to play after a verb is cleared.

7. **Combine emotion and verb.** `combineEmotionVerbFace()` walks all 28
   `FaceParams` fields and for each runs `combineEmotionVerbField()`:
   - If the verb has no value for this field (or its strength is 0),
     the emotion wins outright.
   - If the emotion has strength 0, the verb wins outright (avoids
     dragging the verb value toward the emotion's zero default).
   - Otherwise the output is `lerp(emotion_value, verb_value, factor)`,
     where `factor` is shaped by the **verb strength** as the lerp `t`
     and the **emotion strength** as the curve power:
     - Emotion strength = 50 → linear (`factor = t`).
     - Emotion strength < 50 → ease-out (`1 - (1-t)^p`); a low-strength
       emotion lets even small verb strengths dominate. Power scales
       from 1 at es=50 to 5 at es=0.
     - Emotion strength > 50 → ease-in (`t^p`); a high-strength emotion
       holds on until the verb really commits. Power scales from 1 at
       es=50 to 5 at es=100.
   - The output strength is `max(emotion_strength, verb_strength)`.

   Verb strength 0 always yields `factor = 0` (emotion preserved); verb
   strength 100 always yields `factor = 1` (verb wins). In between, a
   smooth curve whose shape is determined by emotion strength. This is
   how `VerbThinking` can override the mouth shape while leaving the eye
   geometry to bleed from the underlying emotion.

8. **Mood-ring smoothing.** Exponentially follow the tweened
   `ring_r/g/b` toward the combined target with τ ≈ 200 ms. The mood
   ring lives in continuous RGB so changing emotions doesn't blink it —
   it cross-fades.

9. **Think tilt.** Only while `VerbThinking`. Every 3–6 s (random per
   flip, range from idle config) flip the sign of an adjustable head
   sway and pupil offset. The transition is smoothed in over 250 ms via
   `smoothstep01()`. On exit the sign normalization in step 2 prevents a
   pop.

10. **Blink.** Schedule and play eyelid closes. Both timing and shape
    come from the per-expression `IdleAnimRow` in the face config:
    randomized period plus a close time (~80 ms default) and open time
    (~130 ms default). The blink amount is passed through to
    `FaceRenderer::drawFace()` which collapses the eye envelope when the
    blink is past 60%.

11. **Idle gaze offset.** Per the active expression's `gaze_style`:
    - `IdleRandom` — small random pupil offsets at idle pace.
    - `Orbit` — slow circular orbit.
    - `ScanX` — back-and-forth horizontal scan.
    - `Off` — pinned forward.

    The live `(gdx, gdy)` is then cross-faded against the snapshot
    captured at the last verb-change edge, using
    `Face::verbTransitionT(now)`. Same fade applies to the bob
    amplitude inside `bodyBobFor`. See
    [`VERB_SYSTEM.md`](VERB_SYSTEM.md#modification-pass-cross-fade) for
    the full picture and what's *not* faded (servo arm, blink).

    **Phase is integrated, not derived.** Gaze Orbit / ScanX use a
    shared `sGazePhaseRad` that advances by `2π * dt / period` each
    frame. Computing `(now % period) / period` from a period that is
    being continuously interpolated by `EmotionBlend::blendedIdleAnim`
    produces high-frequency jitter (small period drift becomes large
    modulo jumps). Same pattern is used for `bodyBob`, the eye/mouth
    wave phases, and the emotion-arm sine — every periodic value whose
    rate can vary mid-frame keeps its own integrator.

12. **Render mode dispatch.** `Scene::renderScene(...)` for face mode,
    `TextScene::renderTextScene(...)` for text or debug mode. Then
    `Display::pushFrame()` DMA-copies the sprite to the panel.

## Final modification pass

The "modification pass" terminology refers to steps 9–11 above: the
blink amount, gaze offset, body bob (covered below), and think tilt are
all applied to the *resolved* emotion+verb FaceParams immediately before
rendering. They do not enter the `FaceParams` blend itself — they are
arguments to `FaceRenderer::drawFace()` and small per-frame additions to
fields like `face_rot` and `face_y`.

The order of application matters:

```
sSmoothedEmotion ← lerp from ctx.base_face_params       (step 5)
combined ← combineEmotionVerbFace(smoothed, verb)        (step 7)
combined.face_rot += thinkTiltSigned                     (step 9)
combined.face_y += breathOffset (sine, ~4 s period)      (within tick)
gaze_dx, gaze_dy ← idleGlanceFor(expression)             (step 11)
blinkAmt ← currentBlinkAmount()                          (step 10)
drawFace(sprite, combined, blinkAmt, gaze_dx, gaze_dy, ...)
```

`face_y` is also subject to the "compression hack" inside `FaceRenderer`:
half of any vertical translation is used as a global vertical squash so
the eyes and mouth stay inside the round display when the face bobs down.

## Body bob

Body bob is the vertical motion that keeps the face in time with the arm
swing. Two things make it correct:

1. **Phase is integrated, not sampled.** `sBodyBobPhaseRad` advances by
   `2π * dt / periodMs`. If the period changes mid-cycle (e.g. the
   active emotion blends to one with a different arm period) the phase
   is continuous and you don't get a jump.

2. **Period source is the arm.** `MotionBehaviors::periodMsForContext(ctx)`
   tells the face exactly what period the arm is using right now —
   either from `kMotion[expression]` for verbs/overlays, or from the
   blended `ctx.base_emotion_arm` for emotion mode. If you change a
   state's `period_ms` in `FACE_CONFIG_DATA.h`, the face auto-resyncs
   on the next frame.

The bob's amplitude is read from the same source.

## Mood ring color

`FaceParams::ring_r/g/b` is a per-emotion color baked into the preset
table. `EmotionBlend` interpolates them like any other field, weighted
by strength. Verbs that want to change the ring color set
`ring_r/g/b` (with strength) in their sparse-override row in
`FACE_CONFIG_DATA.h` — for example `VerbThinking` pulls the ring
toward a blue-purple. `MoodRingRenderer::moodRingShouldDraw(expression)`
suppresses the ring entirely for a whitelist of states (e.g. Neutral,
Sleeping, Waking), to avoid a permanent ring that distracts from the
geometry.

## Stream effects

`EffectsRenderer` draws the read/write code-stream overlays. Both
streams use a hash-based pseudo-random indentation per scan line so they
*look* like code without having to render real glyphs. The alpha is
driven by FrameController and crossfades over ~100 ms when the
corresponding verb starts or ends. While either stream is non-zero, the
face render rate is bumped to 60 Hz so the scroll stays smooth.

## Activity dots

`ActivityDots` draws two arcs of small dots:

- **Reads** — bottom arc, `+π/2` from the centre.
- **Writes** — top arc, `-π/2`.

Counts come from `SceneContext.read_tools_this_turn` and
`write_tools_this_turn`. The arc widens and the dot size shrinks as the
count grows. When the turn ends (`Happy → Neutral`), the live counters
reset, but the captured `fade_read_count` / `fade_write_count` in
`SceneRenderState` keep the rings visible for ~280 ms while their alpha
fades out, so a finished turn doesn't drop the rings instantly.
