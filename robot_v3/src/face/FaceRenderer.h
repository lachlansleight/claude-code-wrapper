#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file FaceRenderer.h
 * @brief Eyes + mouth procedural draw. The "face shape" half of the scene.
 *
 * Stateless module. Given a fully-resolved `FaceParams` and a few
 * per-frame inputs (blink, gaze offset, expression, time, fg/bg
 * colours), it draws two eyes and a mouth into the supplied sprite.
 * Geometry obeys the anchor constants in `SceneTypes.h` and respects
 * whole-face rotation (`face_rot`) about `(kCx, kPivotY)` plus a
 * vertical offset (`face_y`).
 *
 * Rendering is fully parametric: there are no expression-specific
 * branches. Every shape is described by `(top_apex, top_corner,
 * bot_apex, bot_corner)` plus a wave modulation, all of which
 * interpolate continuously so cross-expression tweens never pop.
 *
 *  - Eye: per-column scan inside `[-eye_rx, +eye_rx]`. Top stroke band
 *    (`eye_thick` outward above the top edge) and bot stroke band
 *    (same below the bottom edge) are painted in fg; the hollow
 *    interior shows the pupil drawn behind. Strokes extend outward so
 *    the two bands never overlap. Out-of-envelope columns are not
 *    touched, so the pupil is naturally clipped to the eye shape.
 *  - Mouth: per-column solid fill between top and bottom curves.
 *    `mouth_thick` is the minimum band thickness when the curves
 *    collapse (closed-mouth case).
 *  - Wave modulation: per-shape sinusoid (`*_wave_amp`, `*_wave_freq`,
 *    `*_wave_speed`) shifts both edges of the shape together, giving
 *    e.g. the long-execution mouth zigzag without any hard branches.
 *    Zero amplitude → no effect.
 */
namespace Face {

/**
 * Draw the face into @p s. Caller is responsible for clearing the
 * sprite first; this only paints over the eye/mouth regions.
 *
 * @param p         Geometry (already mood-ring-baked is fine; ring_* is ignored here).
 * @param blinkAmt  0 = open, 1 = fully closed; squeezes the eye envelope vertically.
 * @param gdx,gdy   Gaze offset added to `pupil_dx,dy` in eye-local frame.
 * @param expr      Currently unused (kept for ABI symmetry / future per-expression hooks).
 * @param nowMs     Wall time in ms; drives wave-modulation phase.
 * @param fg565     Foreground (eyes outline / pupil / mouth fill).
 * @param bg565     Background (eye-iris cutout fill).
 */
void drawFace(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
              Expression expr, uint32_t nowMs, uint16_t fg565, uint16_t bg565);

}  // namespace Face
