#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file FaceRenderer.h
 * @brief Eyes + mouth procedural draw. The "face shape" half of the scene.
 *
 * Stateless module. Given a fully-resolved `FaceParams` and a few
 * per-frame inputs (blink, gaze offset, expression for mouth-shape
 * dispatch, fg/bg colours), it draws two eyes and a mouth into the
 * supplied sprite. Geometry obeys the anchor constants in `SceneTypes.h`
 * and respects whole-face rotation (`face_rot`) about `(kCx, kPivotY)`
 * plus a vertical offset (`face_y`).
 *
 * Implementation details kept private to the .cpp:
 *  - eyes are filled ellipses with a smaller bg-colour cutout for the
 *    iris hollow, plus a separate filled-circle pupil that respects
 *    gaze (clipped to stay inside the iris);
 *  - if `eye_curve` is non-zero (or the eye is squeezed nearly shut by
 *    a blink), the eye degenerates to a parabolic stroke instead;
 *  - the mouth picks between a parabola, an open half-ellipse, an
 *    open full ellipse, or — for `VerbStraining` — a fixed zigzag.
 */
namespace Face {

/**
 * Draw the face into @p s. Caller is responsible for clearing the
 * sprite first; this only paints over the eye/mouth regions.
 *
 * @param p         Geometry (already mood-ring-baked is fine; ring_* is ignored here).
 * @param blinkAmt  0 = open, 1 = fully closed; squashes the eye `ry`.
 * @param gdx,gdy   Gaze offset added to `pupil_dx,dy` before rotation.
 * @param expr      Used to switch mouth style (e.g. zigzag for VerbStraining).
 * @param fg565     Foreground (eyes/mouth fill).
 * @param bg565     Background (eye-iris cutout fill).
 */
void drawFace(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
              Expression expr, uint16_t fg565, uint16_t bg565);

}  // namespace Face
