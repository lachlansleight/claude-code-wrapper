# Face render reference — Visual contract

What the **editor preview** and **firmware vector pass** must agree on after
**pipeline B**: the **curve model** and **pixel semantics** for the
**resolved** face (scalar `int16_t` / RGB integers **after** emotion, verb,
and idle application — see [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)
and [`08_DECISIONS.md`](08_DECISIONS.md)).

Legacy **`int16_t`-only `FaceParams`** literals for migration live in
[`01_CURRENT_STATE.md`](01_CURRENT_STATE.md).

## Display

- **240 × 240** round TFT.
- Centre: `kCx = 120`, `kCy = 120`.
- Eye baseline Y: `kEyeY = 95`.
- Left / right eye centre X: `kEyeLX = 85`, `kEyeRX = 155`.
- Mouth baseline Y: `kMouthY = 165`.
- Whole-face rotation pivot Y: `kPivotY = 130` (chin-anchored tilt).
- **+y down**, **+x right**.

## Geometry fields (resolved values)

At **draw time**, each channel is a **scalar** in the same units as today.
**Authoring** stores **`ParamI16 { value, strength }`** per channel for
emotion presets; verbs and idle policy may also contribute — resolver runs
first ([`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)).

All geometry fields are **pixels** (or **degrees** for `face_rot`, **deg/s**
for wave speed) unless noted.

### Eyes

Two curves (top, bottom) + pupil behind. Hollow interior; strokes drawn
**outward**; sharp corners.

| Field | Meaning |
|-------|---------|
| `eye_dy` | Vertical offset of both eyes from `kEyeY`. |
| `eye_rx` | Eye half-width (`2*eye_rx` total width). |
| `eye_top_apex` / `eye_top_corner` | Top edge Y at `lx=0` / `lx=±eye_rx` (eye-local). |
| `eye_bot_apex` / `eye_bot_corner` | Bottom edge Y at `lx=0` / `lx=±eye_rx`. |
| `eye_thick` | Stroke thickness (outward). |
| `eye_wave_*` | Sinusoidal vertical shift on **both** edges; `speed` in **deg/s**. |
| `pupil_dx`, `pupil_dy`, `pupil_r` | Pupil offset and radius (`0` = no pupil). |

**Interpolation** between apex and corner along `lx` is a **semicircular
arc** (not linear). Pupil is clipped by the envelope columns — it does **not**
shrink when the eye is short.

### Mouth

Filled band between top and bottom curves; same apex/corner model.
`mouth_thick` is the **minimum** band when curves collapse.

### Whole-face

| Field | Meaning |
|-------|---------|
| `face_rot` | Rotation in **degrees** about `(kCx, kPivotY)`. |
| `face_y` | Vertical translation of the whole face drawing (before / in addition to idle bob — exact stacking is firmware; bob **application** is post-verb per `08`). |

### Mood ring

RGB888 channels **`ring_*`**, tweened with geometry during **emotion blend**
of those channels. **`mood_ring_enabled`** is a separate boolean on the
expression row ([`02_FACE_CONFIG_H_SPEC.md`](02_FACE_CONFIG_H_SPEC.md)).
Black RGB alone is not sufficient to hide the ring if the flag is on.

## `EmotionArmMotion`

Blended in `(v, a)` like face fields (four floats / ints — see
`EmotionBlend` today). Stored on **`kEmotions[]`** in **`FACE_CONFIG.h`**
after Phase 1.

## Blending vs drawing

- **Authoring-time blend** (Delaunay, strengths, verb overrides) is specified
  in **`06` + `08`** — not plain linear row average anymore when strengths
  differ.
- **Drawing** consumes **one resolved scalar row per frame** plus any
  **post-verb** lid/pupil/bob adjustments from the idle subsystem.

## RGB565 vs RGB888

Firmware blends in RGB888 space; display is RGB565:

```cpp
uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                    ((uint16_t)(g & 0xFC) << 3) |
                    ((uint16_t)(b & 0xF8) >> 3));
}
```

Editor preview may use RGB888 until quantisation is needed for pixel tests.
