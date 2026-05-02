# Art Collaboration: Bringing an Artist Into the Face Design

Notes on how to let a non-coder artist contribute face designs while
keeping the existing animation system (tweens between states, procedural
modulators like blink / breath / gaze wander / head tilt) working.

## The constraint to design around

The current face animates well because every element is *parameterised*
by `FaceParams`:

- `eye_ry` shrinks during a blink
- `pupil_dx` rotates with the head tilt
- `mouth_open_h` lerps from 0 to 14 entering FINISHED
- etc.

If the artist hands us an arbitrary SVG path with no semantic structure,
we can't easily "blink" it without a model for what blinking *means* for
that shape. Whatever pipeline we pick has to preserve the parameterised
animation surface.

## Recommended approach: semantic SVG layers + polyline export

The pipeline:

1. **Artist works in Inkscape / Illustrator / Figma** with a template SVG
   that has named layers, one per face element:
   ```
   left_eye_outline
   left_eye_pupil
   right_eye_outline
   right_eye_pupil
   mouth_closed
   mouth_open_d
   mouth_open_oval
   eye_arc_happy           (the ^_^ shape for FINISHED)
   eye_line_closed         (the flat line for SLEEP / mid-blink)
   ```
   Each layer is one shape (path or basic primitive). The artist can
   draw whatever shape they want for each named layer.

2. **Build-time Python tool** parses the SVG, flattens each named layer's
   path to a polyline at a target resolution (say 30-60 points per shape),
   and emits a C header with PROGMEM arrays:
   ```c
   const PROGMEM int16_t kEyeOutline[] = { x0, y0, x1, y1, ... };
   const uint8_t          kEyeOutlineCount = 42;
   ```
   Tools that already do most of this work: `svgpathtools` (Python),
   Inkscape's "flatten beziers" + node export.

3. **Firmware adds a polyline renderer**:
   ```c
   void drawPolyline(const int16_t* pts, uint8_t count,
                     float sx, float sy,           // scale
                     int16_t cx, int16_t cy,       // position
                     float cosA, float sinA,       // rotation
                     uint16_t color);
   ```
   Each face element gets drawn by reading its polyline from PROGMEM and
   applying the current FaceParams' scale / position / rotation.

4. **`FaceParams` semantics shift slightly** — the existing fields
   (`eye_rx`, `eye_ry`, `pupil_dx`, `face_rot`, …) become *transform
   parameters* applied to the artist's shape rather than primitive
   dimensions. Blinks still work (squash the polyline vertically); head
   tilts still work (rotate it); tween still works on the transform
   params.

### Sizes

- ~60 points × 4 bytes per coordinate = 240 bytes per shape.
- ~6-10 shapes total ≈ 1.5-2.5 KB of flash. Trivial — we have megabytes.

### What works well

- Artist works in familiar tools.
- Existing animation system carries over with minimal change.
- Tween / blink / head-tilt / gaze still apply on top of artist shapes.
- Cheap memory footprint — happy to ship many alternate "face packs".

### Limitations to flag up front

- **Can't tween between different polylines** (round eye → cat eye, etc.).
  For state-specific shape *changes* (e.g. open mouth vs closed mouth,
  happy ∩ vs round eye), we'd:
  - **Swap shapes** at the state-change moment (snap), or
  - **Implement vertex-by-vertex morph** between two shapes — requires
    both shapes to have the same point count, which the build tool can
    enforce by resampling.
- **Anti-aliasing**: polylines via `drawLine` won't be as smooth as the
  current `fillSmoothCircle` for round shapes. TFT_eSPI has
  `drawSmoothLine` we could use; for filled interiors we'd need a
  scanline polygon fill (modest effort).

## Alternative: full-frame bitmaps per state

If artistic control matters way more than in-state animation:

- Artist draws each state as a complete 240×240 bitmap.
- Encode / compress, store in PROGMEM, blit per state.

**Pros**: max art freedom, no shape constraints.
**Cons**: tweening becomes cross-fade not shape morph; procedural
modulators (blink, breath, gaze, head tilt) hard to apply on top of a
pre-rendered bitmap.

Probably *not* what we want given how much character the current
parameterised system already has — but worth knowing the option exists.

## Bonus: a browser-based preview tool

To save the artist from flashing the device on every iteration, build a
small web app that:

- Loads their SVG
- Applies the same polyline → transform pipeline (ported to JS / Canvas)
- Shows the face animating with the same modulators as on-device

Same build pipeline, just dual-targeting. Tight feedback loop without
needing hardware.

## Suggested first step

Don't rebuild everything at once. Pick **one element** — `mouth_open_d`
is a good candidate, since it's already a custom shape we hand-rolled
(`drawHalfEllipse`). Build the toolchain end-to-end for just that one
element:

1. Make a template SVG with one named layer.
2. Write the SVG → polyline → header tool (small Python script).
3. Add the `drawPolyline` renderer to `Face.cpp`.
4. Replace `drawHalfEllipse` with the polyline-based version.

Once that pipeline works for one shape, scaling up is mechanical — the
build tool processes more layers, the firmware swaps more renderers.

## Open questions for the artist conversation

1. What kind of look do they want — kawaii, retro pixel, mascot, sketchy?
   That'll inform whether smooth curves matter (anti-aliasing effort) or
   chunky outlines are fine (cheap polylines).
2. Do they want shape *variety per state* (cat eyes for thinking, round
   for finished, etc.) or one consistent set of shapes for the whole
   personality? The latter is much simpler.
3. Are they comfortable with named layers as a constraint, or do they
   want to draw each state as a single image? That decision shifts the
   pipeline between the polyline approach and the bitmap approach.
4. Do we want a "skin" system — multiple complete face packs the firmware
   can swap between? Cheap to do once the pipeline exists.
