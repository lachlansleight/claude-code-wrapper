# `FaceParams` shape model: `open_amt` / `arc_amt`

Replaces the four-field-per-shape (`top_apex`, `top_corner`, `bot_apex`,
`bot_corner`) model in `FaceParams` with two new fields per shape:
`open_amt` and `arc_amt`. Drives both eyes and mouth.

This document captures the model definition, design decisions, and the
JS→C++ migration plan so future work can pick it back up without
re-deriving anything.

## Background

The old model directly stored the four control points of two
semicircular arcs:

- `*_top_apex`: y at lx=0 of the top edge (in shape-local coords, +y = down).
- `*_top_corner`: y at lx=±halfw of the top edge.
- `*_bot_apex`, `*_bot_corner`: same for the bottom edge.

The renderer interpolates these with
`curveAt(apex, corner, n) = corner + (apex − corner) · √(1 − n²)`, then
fills the column between the top and bottom edges (mouth) or strokes
each band outward (eye).

The four-knob system is expressive but unintuitive to author and
encourages preset combinations that don't generalise (e.g. asymmetric
apex magnitudes that don't tween cleanly between expressions). The new
model collapses each shape to two intuitive scalars: how "open" it is
and how lopsided the top/bottom split is.

## The new model

Per shape:

- `open_amt`: half-height of the base ellipse (sets where the top and
  bottom **apexes** sit in screen space, ±`open_amt` from shape centre).
- `arc_amt`: signed shape skew in [−2, +2], stored as scaled int16
  (centi-units, so the field range is `[−200, +200]`).

Eye-local coords, +y = down. Let `O = open_amt`, `A = arc_amt / 100`:

- The **apexes** of the arcs stay locked in screen space at ±O.
- The **shared corner** (where top and bottom arcs meet at the side
  edges) slides up or down as `A` varies.

Regimes:

| `A`           | top apex y | bot apex y | corner y (shared) | shape                                  |
|---------------|-----------:|-----------:|------------------:|----------------------------------------|
| 0             | −O         | +O         | 0                 | symmetric ellipse                      |
| 0 → +1        | −O         | +O         | −O · A            | corner slides up, top arc shallows     |
| +1            | −O         | +O         | −O                | D-shape, flat top                      |
| +1 → +2       | (A−2)·O    | +O         | −O                | top arc inverts and dips toward bottom |
| +2            | 0          | +O         | −O                | top arc is half as deep as bottom      |
| negative `A`  | mirror everything vertically                                                  |

We picked **option (a) linear corner placement** in [−1, +1]
(`corner = −S · A`, see curl floor below). At `A = ±0.5` the corner
sits halfway between centre and the top/bottom edge, giving a 1:3
top:bot depth ratio (not 1:2). Validated visually in
`control/p5_test.html`.

The renderer keeps using the existing `curveAt(apex, corner, n)`
primitive. Conversion from `(open_amt, arc_amt)` to
`(top_apex, bot_apex, corner)` happens once at the top of
`drawMouth` / `drawEye`. Everything downstream — column-major fill,
outward strokes, wave overlay, blink scaling, pupil mask, mouth
minimum-thickness — is unchanged.

### Curl floor `K`

A naïve `corner = −open_amt · A` collapses at `open_amt = 0`, so a
closed mouth cannot smile or frown (every control point sits at y=0
and `mouth_thick` just draws a flat horizontal band).

Fix: replace `open_amt` with `S = max(open_amt, K)` wherever it
positions the corner or the inverted apex. Apex anchors stay at
`±open_amt`. With `K = 20`, a closed mouth still gets a corner throw
of up to ±20 px, which `mouth_thick` resolves into a clearly visible
smile / frown band. Once `open_amt ≥ K`, `S == open_amt` and the
formula is identical to the un-floored version.

Sign convention:

- **Mouth** — positive `arc_amt` → smile (∪); negative → frown (∩).
  Consistent with the open-mouth D-shapes.
- **Eye** — same code path, opposite read on the closed shape:
  positive `arc_amt` gives a ∪ (downturned-squint), negative gives a
  ∩ (happy `^_^`). Tune accordingly per expression.

### `arcParams` reference implementation

```js
const ARC_CURL_FLOOR_K = 20;

function arcParams(openAmt, arcAmtScaled) {
  const O = openAmt;
  const A = arcAmtScaled / 100;
  const S = O > ARC_CURL_FLOOR_K ? O : ARC_CURL_FLOOR_K;
  let topApex, botApex, corner;
  if (A >= 0) {
    if (A <= 1) { topApex = -O;             botApex = +O; corner = -S * A; }
    else        { topApex = -O + (A - 1)*S; botApex = +O; corner = -S;     }
  } else {
    const a = -A;
    if (a <= 1) { topApex = -O; botApex = +O;             corner = +S * a; }
    else        { topApex = -O; botApex = +O - (a - 1)*S; corner = +S;     }
  }
  return { topApex, botApex, corner };
}
```

## Storage decisions

- `open_amt` → `int16_t` field, plain pixels (same units as the old
  apex/corner fields).
- `arc_amt` → `int16_t` field, **centi-units**: stored range is
  `[−200, +200]`, real value is field / 100. Keeps `FaceParams`
  homogeneous (no floats) and matches the rest of the struct.
- Per-shape fields go from 4 → 2, so `FaceParams` net shrinks by 4
  fields and 8 bytes.

## Migration strategy

JS first — `simulator_v3.html` is the preset authoring surface, so we
re-author there and copy values into C++ once they look right.

Order of operations:

1. **Plan doc** (this file).
2. **`control/scripts/face-v3.js`** — add `arcParams` helper. Swap the
   `curveAt` callers in `drawMouth` and `drawEye` and the
   `drawEdgeStroke` calls to consume the derived apex/corner.
3. **`control/scripts/frame-controller-v3.js`** — update
   `PARAM_FIELDS` and rewrite `BASE_TARGETS`. Use a one-shot mechanical
   conversion from the legacy values (see below) as the starting point.
4. **`control/simulator_v3.html`** — replace the four apex/corner
   sliders per shape with two (`open_amt`, `arc_amt`). `arc_amt` slider
   is `−200..+200` step 1.
5. **Verify in simulator**: cycle every expression, scrub blend
   triangulation, exercise static editor, check tweening between every
   pair feels reasonable.
6. **Re-author presets** in the simulator until they look right.
7. **Port to firmware** (lockstep with re-authored values):
   - `robot_v3/src/face/SceneTypes.h` — `FaceParams` field swap.
   - `robot_v3/src/face/FaceRenderer.cpp` — `arcParams` helper +
     callers in `drawMouth` / `drawEye` / `drawEdgeStroke`.
   - `robot_v3/src/face/FrameController.cpp` — `kBaseTargets` table
     + `blendFaceParams` field list.
   - `robot_v3/src/behaviour/EmotionBlend.cpp` — field enumeration.
8. **Docs**: `docs/firmware/DISPLAY_AND_FACE.md` model description.

`robot_v2/*` stays on the old model — out of scope.

## Mechanical conversion from legacy presets

For each preset, given the old `(eye_dy, eye_top_apex, eye_top_corner,
eye_bot_apex, eye_bot_corner)` (and the same four for mouth), the
seed values for the new model are:

```
extremes  = [top_apex, top_corner, bot_apex, bot_corner]
top_y     = min(extremes)            // highest point of the shape (most -y)
bot_y     = max(extremes)            // lowest point  of the shape (most +y)
new_dy    = old_dy + round((top_y + bot_y) / 2)
open_amt  = round((bot_y - top_y) / 2)
arc_amt   = 0
```

i.e. fit a symmetric ellipse to the same vertical extent and absorb
any vertical bias into `*_dy`. After the swap every preset starts as
"symmetric ellipse, same overall height as before, at the same
vertical centre". From there we re-author by tweaking `arc_amt` (and
nudging `open_amt` / `*_dy`) until each expression reads correctly.

## Known losses requiring re-author

The new model can't represent all current shapes. Confirmed casualties
(non-exhaustive):

- **Sleepy / Depressed eye droop** — both corners offset to +10/+11.
  The old shape had an asymmetric "saggy" arc distribution; the new
  shape after mechanical conversion is a symmetric ellipse at the
  same vertical centre, losing the droopy arc character. We re-author
  with positive `arc_amt` to bias the corner upward.
- **Distressed eye asymmetric apexes** (−26 top / +33 bot) — old
  shape was tall-below-the-line. New mechanical conversion centres it.
  Re-author with `arc_amt` skew.
- **Distressed / Disappointed mouth crescents** — both arcs curve the
  same direction (full crescent above/below centre). Not directly
  representable in the new two-arc model. Re-author as a frown-D via
  negative `arc_amt` ≈ −1.5, sitting above mouth centre via
  `mouth_dy`.
- **Neutral mouth (smile band)** — old top_apex=bot_apex=+2 with
  corners at 0 produced a thin smile band (mouth_thick kicks in).
  New mechanical conversion is `open_amt = 1`. Re-author as
  `open_amt ≈ 0`, `arc_amt < 0` to keep the smile-like shape.

`mouth_thick` (minimum band thickness) is unchanged and still applies
after derivation, so very-closed mouths still draw.

## Wave / blink / thick / pupil interaction

All unchanged. The derivation happens *before* the existing per-column
logic:

- `blinkScale` multiplies the derived (apex, corner) together — visual
  `arc_amt` is preserved as the eye closes.
- Wave is added on top of the derived curve, same as today.
- `mouth_thick` still enforces minimum band thickness after fill.
- Pupil rendering is independent of arc shape.

## Status

- [x] Validated arc geometry interactively (`control/p5_test.html`).
- [ ] JS renderer + frame controller + simulator.
- [ ] Re-author presets in simulator.
- [ ] Firmware port.
- [ ] Docs.
