# Scene composition and rendering

The renderer is split into a thin orchestrator (`Scene` / `TextScene`)
that dispatches by `RenderMode`, and a set of stateless drawing
modules. All drawing happens into a single 240×240×16-bit sprite that
`Display::pushFrame()` then DMAs to the GC9A01 panel.

## Render modes

`SceneContext::render_mode` (a `RenderMode` enum) has three values:

- `Face` — the animated face. Scene composition described below.
- `Text` — circular status display: title, subtitle, body text.
- `Debug` — full-screen left-aligned diagnostic dump.

`FrameController::tick()` reads `ctx.render_mode` and dispatches:
`Face` → `Scene::renderScene()`, `Text` and `Debug` → `TextScene::
renderTextScene()`.

The mode is set by the bridge via a `config_change` control frame
(handled by `BridgeControl`, applied by `EventRouter` to both
`Settings::setFaceModeEnabled()` and `AgentEvents::setRenderMode()`).
It's a runtime user preference, not a behavioural state.

## Face scene Z-order

`Scene::renderScene()` composes a face frame by drawing in this order
into the sprite:

1. **Background fill.**
2. **`drawFace()`** — eyes and mouth (`FaceRenderer`).
3. **`drawOverlayEffects()`** — waking rim fade or attention rim pulse
   if those overlays are active.
4. **`drawEffects()`** — read/write code-stream overlays
   (`EffectsRenderer`).
5. **`drawMoodRing()`** — smoothed `(r,g,b)` from `FaceParams`; no-op when
   all channels are zero (`MoodRingRenderer`).
6. **`drawActivityDots()`** — read arc at the bottom, write arc at the
   top, sized by per-turn tool counts (`ActivityDots`).

Activity dots and mood ring sit *over* the face deliberately — they're
on the perimeter where the face geometry doesn't reach.

## Layout constants

`SceneTypes.h` fixes the geometric anchors for the renderers:

| Constant     | Value | Meaning                                          |
|--------------|-------|--------------------------------------------------|
| `kCx`, `kCy` | 120   | Screen centre.                                   |
| `kEyeY`      | 95    | Eye centreline.                                  |
| `kEyeLX`     | 85    | Left-eye centre x.                               |
| `kEyeRX`     | 155   | Right-eye centre x.                              |
| `kMouthY`    | 165   | Mouth centreline.                                |
| `kPivotY`    | 130   | Rotation pivot. Below centre so chin-anchored.   |

`face_rot` rotates the whole face about `(kCx, kPivotY)`. Because the
pivot is below the centre, a tilt looks like a head-tilt rather than
an eyeball-roll. `face_y` translates the face vertically, with half
the offset re-applied as a global vertical squash so eyes and mouth
remain inside the round display when the face bobs down.

## SceneContext and SceneRenderState

`SceneContext` (in `SceneTypes.h`) is a flat struct populated by
`SceneContextFill`. It carries everything the face needs for one
frame: the effective expression, the smoothed emotion arm preset,
the resolved `FaceParams` from the emotion blend, the palette colours
in both RGB888 and RGB565, the per-turn tool counts, the text
strings, and a pile of diagnostic fields (snapped emotion, verb
chain, held drivers).

`SceneRenderState` is the small "derived for this frame" struct
(stream alphas, fade-out counts and timestamps, precomputed RGB565
colours). `FrameController` passes it down to the renderers along
with the `SceneContext`.

## FaceRenderer — eyes and mouth

`FaceRenderer::drawFace(sprite, faceParams, blinkAmt, gdx, gdy,
expression, nowMs, fg565, bg565)` draws everything except overlays
and the mood ring. It is **stateless**: every per-frame transient
(blink, gaze, body bob, think tilt) is already baked into its inputs.

### Eye geometry

Each eye is a vertical scan from `-eye_rx` to `+eye_rx` (its half-width
in pixels). The top and bottom edges are interpolated curves controlled
by four `FaceParams` fields per eye region:

```
y_top(n) = eye_top_corner + (eye_top_apex - eye_top_corner) * sqrt(1 - n²)
y_bot(n) = eye_bot_corner + (eye_bot_apex - eye_bot_corner) * sqrt(1 - n²)
```

where `n` ∈ [-1, +1] is the normalized horizontal position. The result
is an ellipse-like envelope where moving `apex` raises/lowers the
midline and moving `corner` controls the outer corners. This is what
gives different emotions distinct eye shapes (sad eyes droop at the
corners, happy eyes lift at the corners, surprised eyes are tall
ellipses, etc.).

The eye is drawn as:

1. **Pupil** — anti-aliased circle of radius `pupil_r` at the eye
   centre, offset by `(pupil_dx, pupil_dy) + (gdx, gdy)`.
2. **Interior fill** — clip the pupil and any interior content to the
   ellipse envelope.
3. **Edge strokes** — top and bottom strokes drawn outward by
   `eye_thick` pixels.

### Wave modulation

`wave_freq`, `wave_amp`, `wave_speed` add a sinusoidal wobble to the
top edge for the spaced-out look used by `Distressed`, `Blissed`, etc.
The phase is **integrated** by `FrameController` (`phase += wave_speed *
dt_ms * π / 180000`) and passed into `drawFace` via
`SceneRenderState.eye_wave_phase_rad` / `mouth_wave_phase_rad`. The
renderer never recomputes phase from `nowMs * wave_speed` because that
would jitter when `EmotionBlend` is continuously interpolating
`wave_speed` as V/A drifts — multiplying a moving speed by a large
`nowMs` magnifies tiny per-frame speed changes into huge phase jumps.
The phase wraps
naturally inside `sinf`. Setting any of these to zero disables the
effect on that field.

### Blink

`blinkAmt ∈ [0, 1]` collapses the eye envelope vertically:

```
blinkScale = max(0, 1 - blinkAmt)
```

At `blinkAmt > 0.6` the envelope collapses to a flat line (lid
closed). The pupil is clipped by the envelope so it disappears
naturally as the lids meet.

### Mouth

Drawn between two curves built the same way as the eyes (`mouth_*`
fields), with a minimum thickness `mouth_thick` so that even a closed
mouth has a visible line.

### Whole-face transform

After computing each scan column, `paintLocalSpan()` rotates the
column by `face_rot` degrees about `(kCx, kPivotY)` and translates by
`face_y`. A 2-pixel anti-seam stamp prevents diagonal gaps when
rotation is non-trivial.

## EffectsRenderer — code stream overlays

Two procedural "scrolling token" overlays:

- **Read stream** (left, fast). Narrow lines, fast scroll. Visible
  during `VerbReading`.
- **Write stream** (right, slower). Chunkier blocks, slower scroll.
  Visible during `VerbWriting`.

Indentation per scan line is hash-derived so it *looks* like real code
without rendering glyphs. Alpha is supplied by FrameController and
crossfades over ~100 ms when the corresponding verb activates.

`drawOverlayEffects()` in the same module also handles the **waking
overlay** (rim fade-in over a short window) and the **attention
overlay** (pulsing rim) used by the `OverlayWaking` and
`OverlayAttention` expressions.

## MoodRingRenderer

A 6-px ring at radii 110–115 from the screen centre. Colour comes
from `FaceParams::ring_r/g/b` after smoothing (200 ms τ inside
FrameController). Behaviour:

- No-op if `(r, g, b) == (0, 0, 0)` — ring visibility follows effective
  `ring_*` from emotion blend and verb keyframes, not an expression allow-list.

Ring colour still eases in/out via FrameController's mood-ring low-pass.

## ActivityDots

Two arc rings of small dots:

- **Reads** — bottom arc, centred at `+π/2`, growing outward as
  `read_tools_this_turn` increases.
- **Writes** — top arc, centred at `-π/2`, similarly.

Geometry: arc width grows with count, dot size shrinks. When a turn
ends (transition `Happy → Neutral`), `FrameController` captures the
counts in `SceneRenderState.fade_*_count` and the dots fade out over
~280 ms while the live counters reset to 0.

Tint is the accent `NamedColor` for the active expression — see the
table in [`VERB_SYSTEM.md`](VERB_SYSTEM.md).

## TextScene

`TextScene::renderTextScene()` draws either text or debug mode.

### Text mode (circular)

Layout (constants from `TextScene.cpp`):

```
title       (centred, top + small pad)
subtitle    (current tool name, or elapsed seconds)
divider     (horizontal line)
body        (wrapped, up to ~8 lines, kLineAdvanceBody = 14 px)
```

Wrapping uses `circleChordAtY()` to compute the available
`[xMin, xMax]` chord at each scan line so text never escapes the round
display. `kCircleRadius = 116` with a 5 px padding inside.

The text content comes straight off the `SceneContext`:

| Field                  | Source / use                                                  |
|------------------------|---------------------------------------------------------------|
| `status_line`          | High-level status, e.g. "Thinking", "Done"                    |
| `subtitle_tool`        | Current tool subtitle, with linger smoothing                  |
| `body_text`            | Streaming assistant or thinking text                          |
| `latest_shell_command` | Most recent shell command for diagnostic                      |
| `latest_read_target`   | Most recent file path read                                    |
| `latest_write_target`  | Most recent file path written                                 |

Every string is sanitized via `AsciiCopy` before reaching this point,
so non-ASCII codepoints are folded to ASCII equivalents (curly quotes
→ straight, em-dash → `--`, ellipsis → `...`, arrows → `->`, etc.).

### Debug mode

A left-aligned full-screen dump used while developing:

- Effective expression name
- Mood `(v, a)` and snapped emotion
- Verb chain (`current` / `effective` / time in current / linger)
- Held drivers and their targets
- Connection state

This is the easiest place to read live behaviour state from the
device.

## FACE_CONFIG and FACE_CONFIG_DATA

The configuration data is split into:

- **`FACE_CONFIG.h`** — types, lookup helpers (`isEmotionExpression`,
  `expressionForNamedEmotion`, `moodRingShouldDraw`), and references to
  the data tables.
- **`FACE_CONFIG_DATA.h`** — the actual tables. This is the file you
  edit to change behaviour data:
  - `Expression` enum (22 entries: 14 emotions, 6 verbs, 2 overlays).
  - `NamedEmotion` enum (14 entries) and the
    `kNamedEmotionToExpression[]` map.
  - `kEmotionPoints[]` — the 14 anchor positions in (v, a).
  - `kBaseTargets[]` — per-expression `FaceParams` preset table (28
    `ParamI16` per row: face, ring, and arm fields). Emotions carry full
    presets; verb rows are `FACE_ROW_EMPTY`; geometry and arm come from
    `kVerbTimelines` overrides.
  - `kVerbTimelines[]` — verb keyframe timeline table (`VerbTimeline`).
  - `kFrameAnim`, `kEmotionSim`, `kVerbSim` — animation and dynamics
    constants (tick interval, blink defaults, smoothing τ values,
    Strain auto-promotion delay, etc.).
  - `kIdleAnim[]` — per-expression idle animation config (blink
    period, gaze style, body bob amplitude — bob **position** follows
    live arm angle; see [`MOTION.md`](MOTION.md)).

Splitting helpers from data lets the data file be regenerated by
external tooling without disturbing C++ logic.

`Expression` enum is the single source of truth for both face
rendering and motion behaviour. The triangulation in
`EmotionTriangulation.h` is generated by
`scripts/gen_emotion_triangulation.py` from the same `kEmotionPoints`
table — keep them in sync by re-running the script after editing
positions.
